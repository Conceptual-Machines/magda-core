#include "plugins/mutable/MutableRingsPlugin.hpp"

#include <array>
#include <cstdint>
#include <cstring>

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable),
// compiled with -DTEST. Walled behind the pimpl so this is the only TU in
// magda_daw that sees the eurorack headers.
#include "rings/dsp/dsp.h"
#include "rings/dsp/part.h"

namespace magda::daw::audio {

namespace te = tracktion::engine;

namespace {

constexpr int kBlock = static_cast<int>(rings::kMaxBlockSize);  // 24
const float kInternalRate = rings::kSampleRate;                 // 48 kHz (runtime const)

const char* const kModelNames[] = {"Modal", "Sympathetic", "String",
                                   "FM",    "Sym Quant",   "String+Verb"};
constexpr int kNumModels = 6;

// 4-point, 3rd-order Hermite (Catmull-Rom) interpolation between x[1] and x[2].
inline float cubic(const float* x, float t) {
    const float a = -0.5f * x[0] + 1.5f * x[1] - 1.5f * x[2] + 0.5f * x[3];
    const float b = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    const float c = -0.5f * x[0] + 0.5f * x[2];
    const float d = x[1];
    return ((a * t + b) * t + c) * t + d;
}

enum class Kind { Normalised, Model, Polyphony, Chord, Pitch, Fine, Level };
struct Desc {
    const char* id;
    const char* name;
    float def;
    Kind kind;
};

const std::array<Desc, MutableRingsPlugin::kNumParams> kDescs = {{
    {"structure", "Structure", 0.25f, Kind::Normalised},
    {"brightness", "Brightness", 0.5f, Kind::Normalised},
    {"damping", "Damping", 0.5f, Kind::Normalised},
    {"position", "Position", 0.25f, Kind::Normalised},
    {"model", "Model", 0.0f, Kind::Model},
    {"polyphony", "Polyphony", 1.0f, Kind::Polyphony},  // index 1 -> 2 voices
    {"chord", "Chord", 0.0f, Kind::Chord},
    {"pitch", "Pitch", 0.0f, Kind::Pitch},
    {"fine", "Fine", 0.0f, Kind::Fine},
    {"level", "Level", 0.0f, Kind::Level},
}};

}  // namespace

//==============================================================================
struct MutableRingsPlugin::Impl {
    Impl() {
        std::memset(silence_, 0, sizeof(silence_));
        // See MutableElementsPlugin: upstream assumes zero-initialised BSS and
        // Part::Init() doesn't reset every byte of state, so zero the
        // heap-allocated Part before Init to avoid garbage filter/voice state
        // ringing on a fresh instance.
        std::memset(static_cast<void*>(&part_), 0, sizeof(part_));
        std::memset(reverbBuffer_, 0, sizeof(reverbBuffer_));
        part_.Init(reverbBuffer_);
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
        strumPending_ = false;
        currentNote_ = 60;
    }

    // Called once per host block: cheap per-block state. set_model self-guards;
    // set_polyphony does not (it always re-spreads notes + flags dirty), so we
    // only call it on an actual change.
    void configure(const rings::Patch& patch, int model, int polyphony, int chord,
                   float transposeSemitones) {
        patch_ = patch;
        chord_ = chord;
        transpose_ = transposeSemitones;
        part_.set_model(static_cast<rings::ResonatorModel>(model));
        if (polyphony != appliedPolyphony_) {
            part_.set_polyphony(polyphony);
            appliedPolyphony_ = polyphony;
        }
    }

    // Each note-on retunes and fires a one-block strum; Rings rotates voices so
    // earlier notes keep ringing (polyphony). No note-off: decay is the Damping.
    void noteOn(int n) {
        currentNote_ = n;
        strumPending_ = true;
    }

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
    inline void nextSource(float& l, float& r) {
        if (chunkPos_ >= kBlock) {
            rings::PerformanceState ps{};
            ps.internal_exciter = true;  // internal plucker; no audio input
            ps.internal_strum = false;   // we drive strum from MIDI
            ps.internal_note = false;    // we supply the note
            ps.strum = strumPending_;
            ps.tonic = 0.0f;
            ps.fm = 0.0f;
            ps.note = static_cast<float>(currentNote_) + transpose_;
            ps.chord = chord_;
            part_.Process(ps, patch_, silence_, out_, aux_, static_cast<size_t>(kBlock));
            strumPending_ = false;
            chunkPos_ = 0;
        }
        l = out_[chunkPos_];
        r = aux_[chunkPos_];
        ++chunkPos_;
    }

    rings::Part part_;
    rings::Patch patch_{};
    uint16_t reverbBuffer_[32768];

    float silence_[kBlock];
    float out_[kBlock];
    float aux_[kBlock];
    int chunkPos_ = kBlock;

    double ratio_ = kInternalRate / 44100.0;
    double frac_ = 0.0;
    float xL_[4]{};
    float xR_[4]{};
    bool primed_ = false;

    bool strumPending_ = false;
    int currentNote_ = 60;
    int chord_ = 0;
    float transpose_ = 0.0f;
    int appliedPolyphony_ = -1;
};

//==============================================================================
const char* MutableRingsPlugin::xmlTypeName = "magda_rings";

MutableRingsPlugin::MutableRingsPlugin(const te::PluginCreationInfo& info)
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
            case Kind::Model:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {0.0f, static_cast<float>(kNumModels - 1), 1.0f},
                    [](float v) {
                        int idx = juce::jlimit(0, kNumModels - 1, juce::roundToInt(v));
                        return juce::String(kModelNames[idx]);
                    },
                    [](const juce::String& s) {
                        for (int k = 0; k < kNumModels; ++k)
                            if (s.equalsIgnoreCase(kModelNames[k]))
                                return static_cast<float>(k);
                        return s.getFloatValue();
                    });
                break;
            case Kind::Polyphony:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {0.0f, 2.0f, 1.0f},
                    [](float v) {
                        return juce::String(1 << juce::jlimit(0, 2, juce::roundToInt(v)));
                    },
                    [](const juce::String& s) {
                        int voices = s.getIntValue();
                        return voices >= 4 ? 2.0f : voices >= 2 ? 1.0f : 0.0f;
                    });
                break;
            case Kind::Chord:
                params_[static_cast<size_t>(i)] = addParam(
                    d.id, d.name, {0.0f, 10.0f, 1.0f},
                    [](float v) { return juce::String(juce::roundToInt(v)); },
                    [](const juce::String& s) { return s.getFloatValue(); });
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

MutableRingsPlugin::~MutableRingsPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : params_)
        if (p != nullptr)
            p->detachFromCurrentValue();
}

void MutableRingsPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate;
    impl_->prepare(sampleRate_);
}

void MutableRingsPlugin::deinitialise() {}

void MutableRingsPlugin::reset() {
    impl_->resetVoice();
}

void MutableRingsPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (fc.destBuffer == nullptr)
        return;

    float v[kNumParams];
    for (int i = 0; i < kNumParams; ++i)
        v[i] = params_[static_cast<size_t>(i)]->getCurrentValue();

    rings::Patch patch;
    patch.structure = v[kStructure];
    patch.brightness = v[kBrightness];
    patch.damping = v[kDamping];
    patch.position = v[kPosition];

    const int model = juce::jlimit(0, kNumModels - 1, juce::roundToInt(v[kModel]));
    const int polyphony = 1 << juce::jlimit(0, 2, juce::roundToInt(v[kPolyphony]));  // 1 / 2 / 4
    const int chord = juce::jlimit(0, 10, juce::roundToInt(v[kChord]));
    const float transpose = v[kPitch] + v[kFine] * 0.01f;
    impl_->configure(patch, model, polyphony, chord, transpose);

    auto* destL = fc.destBuffer->getWritePointer(0, fc.bufferStartSample);
    float* destR = fc.destBuffer->getNumChannels() > 1
                       ? fc.destBuffer->getWritePointer(1, fc.bufferStartSample)
                       : nullptr;

    int pos = 0;
    auto renderTo = [&](int upto) {
        if (upto <= pos)
            return;
        impl_->generate(destL + pos, destR != nullptr ? destR + pos : nullptr, upto - pos);
        pos = upto;
    };

    if (fc.bufferForMidiMessages != nullptr) {
        for (auto& m : *fc.bufferForMidiMessages) {
            if (!m.isNoteOn() || m.getVelocity() == 0)
                continue;  // Rings has no note-off gate; resonators decay via Damping
            int evPos = juce::jlimit(0, fc.bufferNumSamples - 1,
                                     juce::roundToInt(m.getTimeStamp() * sampleRate_));
            renderTo(evPos);
            impl_->noteOn(m.getNoteNumber());
        }
    }
    renderTo(fc.bufferNumSamples);

    const float gain = juce::Decibels::decibelsToGain(v[kLevel]);
    fc.destBuffer->applyGain(fc.bufferStartSample, fc.bufferNumSamples, gain);
}

void MutableRingsPlugin::restorePluginStateFromValueTree(const juce::ValueTree& vt) {
    for (int i = 0; i < kNumParams; ++i) {
        if (auto prop = vt.getPropertyPointer(juce::Identifier(kDescs[static_cast<size_t>(i)].id)))
            values_[static_cast<size_t>(i)] = static_cast<float>(*prop);
    }
    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

}  // namespace magda::daw::audio
