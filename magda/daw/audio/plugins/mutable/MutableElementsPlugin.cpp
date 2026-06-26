#include "plugins/mutable/MutableElementsPlugin.hpp"

#include <array>
#include <cstdint>
#include <cstring>

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable).
// Compiled with -DTEST (set on the magda_mutable target) so stmlib uses its
// portable code paths. Walled behind the pimpl so this is the only TU in
// magda_daw that sees the eurorack headers.
#include "elements/dsp/dsp.h"
#include "elements/dsp/part.h"

namespace magda::daw::audio {

namespace te = tracktion::engine;

namespace {

constexpr int kBlock = static_cast<int>(elements::kMaxBlockSize);  // 16
const float kInternalRate = elements::kSampleRate;                 // 32 kHz (runtime const)

// 4-point, 3rd-order Hermite (Catmull-Rom) interpolation between x[1] and x[2].
inline float cubic(const float* x, float t) {
    const float a = -0.5f * x[0] + 1.5f * x[1] - 1.5f * x[2] + 0.5f * x[3];
    const float b = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    const float c = -0.5f * x[0] + 0.5f * x[2];
    const float d = x[1];
    return ((a * t + b) * t + c) * t + d;
}

// Parameter descriptors, in ParamIndex order.
enum class Kind { Normalised, Pitch, Fine, Level };
struct Desc {
    const char* id;
    const char* name;
    float def;
    Kind kind;
};

const std::array<Desc, MutableElementsPlugin::kNumParams> kDescs = {{
    {"contour", "Contour", 0.5f, Kind::Normalised},
    {"bow", "Bow", 0.0f, Kind::Normalised},
    {"bowTimbre", "Bow Timbre", 0.5f, Kind::Normalised},
    {"blow", "Blow", 0.0f, Kind::Normalised},
    {"blowFlow", "Blow Flow", 0.5f, Kind::Normalised},
    {"blowTimbre", "Blow Timbre", 0.5f, Kind::Normalised},
    {"strike", "Strike", 0.8f, Kind::Normalised},
    {"strikeMallet", "Strike Mallet", 0.5f, Kind::Normalised},
    {"strikeTimbre", "Strike Timbre", 0.5f, Kind::Normalised},
    {"signature", "Signature", 0.1f, Kind::Normalised},
    {"geometry", "Geometry", 0.3f, Kind::Normalised},
    {"brightness", "Brightness", 0.5f, Kind::Normalised},
    {"damping", "Damping", 0.7f, Kind::Normalised},
    {"position", "Position", 0.3f, Kind::Normalised},
    {"space", "Space", 0.3f, Kind::Normalised},
    {"pitch", "Pitch", 0.0f, Kind::Pitch},
    {"fine", "Fine", 0.0f, Kind::Fine},
    {"level", "Level", 0.0f, Kind::Level},
    {"velAmp", "Vel>Amp", 1.0f, Kind::Normalised},
}};

}  // namespace

//==============================================================================
// Impl: the Mutable DSP voice + a streaming 32 kHz -> host resampler + the
// monophonic MIDI state. Lives entirely on the audio thread once initialised.
struct MutableElementsPlugin::Impl {
    Impl() {
        std::memset(silence_, 0, sizeof(silence_));
        // The upstream DSP objects assume zero-initialised BSS (they're statics
        // on the embedded target) and Part::Init() doesn't reset every byte of
        // sub-DSP state. Here Part is heap-allocated with a do-nothing ctor, so
        // without this its filter/exciter history is garbage and a fresh
        // instance can ring "from nowhere". These are non-polymorphic POD-like
        // structs, so zeroing before Init() is safe and matches the target.
        std::memset(static_cast<void*>(&part_), 0, sizeof(part_));
        std::memset(reverbBuffer_, 0, sizeof(reverbBuffer_));
        part_.Init(reverbBuffer_);
        uint32_t seed[3] = {0x12345678u, 0x9abcdef0u, 0x0f1e2d3cu};
        part_.Seed(seed, 3);
        part_.set_resonator_model(elements::RESONATOR_MODEL_MODAL);
    }

    void prepare(double hostRate) {
        ratio_ = kInternalRate / hostRate;
        resetVoice();
    }

    void resetVoice() {
        primed_ = false;
        frac_ = 0.0;
        chunkPos_ = kBlock;
        std::memset(xL_, 0, sizeof(xL_));
        std::memset(xR_, 0, sizeof(xR_));
        heldCount_ = 0;
        gate_ = false;
        strength_ = 0.8f;
        activeNote_ = 60;
        // Part::Panic() is declared upstream but never defined; gate=false lets
        // the resonator decay naturally, which is all reset needs here.
    }

    elements::Patch* patch() {
        return part_.mutable_patch();
    }

    // --- monophonic note handling (last-note priority with a held stack) ---
    void noteOn(int n, float vel) {
        if (heldCount_ < static_cast<int>(held_.size()))
            held_[heldCount_++] = static_cast<uint8_t>(n);
        activeNote_ = n;
        gate_ = true;
        strength_ = vel;
    }

    void noteOff(int n) {
        int w = 0;
        for (int r = 0; r < heldCount_; ++r)
            if (held_[r] != n)
                held_[w++] = held_[r];
        heldCount_ = w;
        if (heldCount_ == 0)
            gate_ = false;
        else
            activeNote_ = held_[heldCount_ - 1];  // legato back to held note
    }

    void setPerformance(float transposeSemitones, float velToAmp) {
        perf_.gate = gate_;
        perf_.note = static_cast<float>(activeNote_) + transposeSemitones;
        // velToAmp blends the note strength between a fixed full level (0, so
        // velocity is ignored) and the played velocity (1, full sensitivity).
        perf_.strength = juce::jlimit(0.0f, 1.0f, 1.0f - velToAmp * (1.0f - strength_));
        perf_.modulation = 0.0f;
    }

    // Render n host-rate samples into outL/outR (outR may be null for mono).
    void generate(float* outL, float* outR, int n) {
        if (!primed_) {
            for (int j = 0; j < 4; ++j)
                nextSource(xL_[j], xR_[j]);
            frac_ = 0.0;
            primed_ = true;
        }
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(frac_);
            outL[i] = cubic(xL_, t);
            if (outR != nullptr)
                outR[i] = cubic(xR_, t);
            frac_ += ratio_;
            while (frac_ >= 1.0) {
                frac_ -= 1.0;
                xL_[0] = xL_[1];
                xL_[1] = xL_[2];
                xL_[2] = xL_[3];
                xR_[0] = xR_[1];
                xR_[1] = xR_[2];
                xR_[2] = xR_[3];
                nextSource(xL_[3], xR_[3]);
            }
        }
    }

  private:
    // One 32 kHz stereo sample; refills a 16-sample DSP block as needed.
    inline void nextSource(float& l, float& r) {
        if (chunkPos_ >= kBlock) {
            part_.Process(perf_, silence_, silence_, main_, aux_, static_cast<size_t>(kBlock));
            chunkPos_ = 0;
        }
        l = main_[chunkPos_];
        r = aux_[chunkPos_];
        ++chunkPos_;
    }

    elements::Part part_;
    elements::PerformanceState perf_{};
    uint16_t reverbBuffer_[32768];

    float silence_[kBlock];
    float main_[kBlock];
    float aux_[kBlock];
    int chunkPos_ = kBlock;

    double ratio_ = kInternalRate / 44100.0;
    double frac_ = 0.0;
    float xL_[4]{};
    float xR_[4]{};
    bool primed_ = false;

    std::array<uint8_t, 16> held_{};
    int heldCount_ = 0;
    bool gate_ = false;
    float strength_ = 0.8f;
    int activeNote_ = 60;
};

//==============================================================================
const char* MutableElementsPlugin::xmlTypeName = "magda_elements";

MutableElementsPlugin::MutableElementsPlugin(const te::PluginCreationInfo& info)
    : Plugin(info), impl_(std::make_unique<Impl>()) {
    auto* um = getUndoManager();

    for (int i = 0; i < kNumParams; ++i) {
        const auto& d = kDescs[static_cast<size_t>(i)];
        values_[static_cast<size_t>(i)].referTo(state, juce::Identifier(d.id), um, d.def);

        switch (d.kind) {
            case Kind::Normalised:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {0.0f, 1.0f},
                    [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; },
                    [](const juce::String& s) { return s.getFloatValue() / 100.0f; });
                break;
            case Kind::Pitch:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {-24.0f, 24.0f, 0.0f},
                    [](float v) { return juce::String(juce::roundToInt(v)) + " st"; },
                    [](const juce::String& s) { return s.getFloatValue(); });
                break;
            case Kind::Fine:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {-100.0f, 100.0f, 0.0f},
                    [](float v) { return juce::String(juce::roundToInt(v)) + " ct"; },
                    [](const juce::String& s) { return s.getFloatValue(); });
                break;
            case Kind::Level:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {-60.0f, 12.0f, 0.0f, 4.0f},
                    [](float v) { return juce::String(v, 1) + " dB"; },
                    [](const juce::String& s) { return s.getFloatValue(); });
                break;
        }

        params_[static_cast<size_t>(i)]->attachToCurrentValue(values_[static_cast<size_t>(i)]);
    }
}

MutableElementsPlugin::~MutableElementsPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : params_)
        if (p != nullptr)
            p->detachFromCurrentValue();
}

void MutableElementsPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    impl_->prepare(sampleRate_);
}

void MutableElementsPlugin::deinitialise() {}

void MutableElementsPlugin::reset() {
    impl_->resetVoice();
}

void MutableElementsPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr)
        return;

    // Pull current parameter values and push them into the DSP patch (block rate).
    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i)
        v[i] = params_[static_cast<size_t>(i)]->getCurrentValue();

    auto* p = impl_->patch();
    p->exciter_envelope_shape = v[kContour];
    p->exciter_bow_level = v[kBow];
    p->exciter_bow_timbre = v[kBowTimbre];
    p->exciter_blow_level = v[kBlow];
    p->exciter_blow_meta = v[kBlowFlow];
    p->exciter_blow_timbre = v[kBlowTimbre];
    p->exciter_strike_level = v[kStrike];
    p->exciter_strike_meta = v[kStrikeMallet];
    p->exciter_strike_timbre = v[kStrikeTimbre];
    p->exciter_signature = v[kSignature];
    p->resonator_geometry = v[kGeometry];
    p->resonator_brightness = v[kBrightness];
    p->resonator_damping = v[kDamping];
    p->resonator_position = v[kPosition];
    p->resonator_modulation_frequency = 0.0f;
    p->resonator_modulation_offset = 0.0f;
    p->reverb_diffusion = 0.625f;
    p->reverb_lp = 0.7f;
    p->space = v[kSpace];
    p->modulation_frequency = 0.0f;

    const float transpose = v[kPitch] + v[kFine] * 0.01f;
    const float velToAmp = v[kVelAmp];

    auto* destL = fc.destBuffer->getWritePointer(0, fc.bufferStartSample);
    float* destR = fc.destBuffer->getNumChannels() > 1
                       ? fc.destBuffer->getWritePointer(1, fc.bufferStartSample)
                       : nullptr;

    // Split the block at each MIDI event so note timing lands at the right
    // sample, generating each segment with the performance state up to it.
    int pos = 0;
    auto renderTo = [&](int upto) {
        if (upto <= pos)
            return;
        impl_->setPerformance(transpose, velToAmp);
        impl_->generate(destL + pos, destR != nullptr ? destR + pos : nullptr, upto - pos);
        pos = upto;
    };

    if (fc.bufferForMidiMessages != nullptr) {
        for (auto& m : *fc.bufferForMidiMessages) {
            if (!m.isNoteOn() && !m.isNoteOff())
                continue;
            int evPos = juce::jlimit(0, fc.bufferNumSamples - 1,
                                     juce::roundToInt(m.getTimeStamp() * sampleRate_));
            renderTo(evPos);
            if (m.isNoteOn() && m.getVelocity() > 0)
                impl_->noteOn(m.getNoteNumber(), m.getFloatVelocity());
            else
                impl_->noteOff(m.getNoteNumber());
        }
    }
    renderTo(fc.bufferNumSamples);

    const float gain = juce::Decibels::decibelsToGain(v[kLevel]);
    fc.destBuffer->applyGain(fc.bufferStartSample, fc.bufferNumSamples, gain);
}

void MutableElementsPlugin::restorePluginStateFromValueTree(const juce::ValueTree& vt) {
    for (int i = 0; i < kNumParams; ++i) {
        if (auto prop = vt.getPropertyPointer(juce::Identifier(kDescs[static_cast<size_t>(i)].id)))
            values_[static_cast<size_t>(i)] = static_cast<float>(*prop);
    }
    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

}  // namespace magda::daw::audio
