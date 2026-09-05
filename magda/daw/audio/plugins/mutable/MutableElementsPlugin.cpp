#include "plugins/mutable/MutableElementsPlugin.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable).
// Compiled with -DTEST (set on the magda_mutable target) so stmlib uses its
// portable code paths. Walled behind the pimpl so this is the only TU in the
// base device pack that sees the eurorack headers.
#include "elements/dsp/dsp.h"
#include "elements/dsp/part.h"

namespace magda::daw::audio {

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

/// One slot's metadata, from the descriptor table. The ids, order and display
/// ranges are pinned to what the retired host-native plugin registered,
/// because projects store parameter values in the model in display units
/// against these ranges and address the slots by index.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    if (index < 0 || index >= MutableElementsPlugin::kNumParams)
        return info;

    const auto& d = kDescs[static_cast<size_t>(index)];
    info.paramIndex = index;
    info.stableId = d.id;
    info.name = d.name;
    info.defaultValue = d.def;

    switch (d.kind) {
        case Kind::Normalised:
            info.minValue = 0.0f;
            info.maxValue = 1.0f;
            info.displayFormat = DisplayFormat::Percent;
            break;
        case Kind::Pitch:
            info.unit = "st";
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            break;
        case Kind::Fine:
            info.unit = "ct";
            info.minValue = -100.0f;
            info.maxValue = 100.0f;
            break;
        case Kind::Level:
            // The retired host-native plugin's NormalisableRange carried skew
            // 4, which JUCE applies as real = min + span * normalized^(1/skew).
            info.unit = "dB";
            info.scale = ParameterScale::Exponential;
            info.skewFactor = 0.25f;
            info.minValue = -60.0f;
            info.maxValue = 12.0f;
            break;
    }

    return info;
}

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

    // Force-release every held note (CC120/123 panic). Without this a dropped
    // note-off would strand the gate high and the resonator would never release.
    void allNotesOff() {
        heldCount_ = 0;
        gate_ = false;
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
            const auto t = static_cast<float>(frac_);
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

MutableElementsPlugin::MutableElementsPlugin() : impl_(std::make_unique<Impl>()) {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

MutableElementsPlugin::~MutableElementsPlugin() = default;

ParameterInfo MutableElementsPlugin::parameterInfo(int index) const {
    return slotInfo(index);
}

float MutableElementsPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void MutableElementsPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float MutableElementsPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

void MutableElementsPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;
    impl_->prepare(sampleRate_);
    scratch_.setSize(2, std::max(context.maximumBlockSize, 1), false, true, false);
}

void MutableElementsPlugin::reset() {
    impl_->resetVoice();
}

void MutableElementsPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    // Pull current parameter values and push them into the DSP patch (block rate).
    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i)
        v[i] = displayValue(i);

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

    auto& buffer = *context.audio;
    const int start = context.startSample;
    const int numChannels = buffer.getNumChannels();
    auto* destL = scratch_.getWritePointer(0);
    auto* destR = numChannels > 1 ? scratch_.getWritePointer(1) : nullptr;

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

    if (context.midi != nullptr) {
        for (int eventIndex = 0; eventIndex < context.midi->size(); ++eventIndex) {
            const auto& m = context.midi->message(eventIndex);
            const bool isPanic = m.isController() &&
                                 (m.getControllerNumber() == 120 || m.getControllerNumber() == 123);
            if (!m.isNoteOn() && !m.isNoteOff() && !isPanic)
                continue;
            const int evPos = juce::jlimit(0, context.numSamples - 1,
                                           juce::roundToInt(m.getTimeStamp() * sampleRate_));
            renderTo(evPos);
            if (isPanic)
                impl_->allNotesOff();  // CC120/123: release any stuck gate
            else if (m.isNoteOn() && m.getVelocity() > 0)
                impl_->noteOn(m.getNoteNumber(), m.getFloatVelocity());
            else
                impl_->noteOff(m.getNoteNumber());
        }
    }
    renderTo(context.numSamples);

    // Add rather than replace (#2370): on the TE leg the buffer may already
    // carry an audio clip's signal that must not be clobbered. The native
    // engine clears an instrument's channels before running it, so add is a
    // no-op difference there.
    const float gain = juce::Decibels::decibelsToGain(v[kLevel]);
    buffer.addFrom(0, start, scratch_, 0, 0, context.numSamples, gain);
    if (destR != nullptr)
        buffer.addFrom(1, start, scratch_, 1, 0, context.numSamples, gain);
}

}  // namespace magda::daw::audio
