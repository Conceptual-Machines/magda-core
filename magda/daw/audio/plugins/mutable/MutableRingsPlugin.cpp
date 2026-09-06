#include "plugins/mutable/MutableRingsPlugin.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable),
// compiled with -DTEST. Walled behind the pimpl so this is the only TU in the
// base device pack that sees the eurorack headers.
#include "rings/dsp/dsp.h"
#include "rings/dsp/part.h"

namespace magda::daw::audio {

namespace {

constexpr int kBlock = static_cast<int>(rings::kMaxBlockSize);  // 24
const float kInternalRate = rings::kSampleRate;                 // 48 kHz (runtime const)

constexpr int kNumModels = 6;

/// One slot's metadata. The ids, order and display ranges are pinned to what
/// the retired host-native plugin registered, because projects store parameter
/// values in the model in display units against these ranges and address the
/// slots by index.
ParameterInfo slotInfo(int index) {
    ParameterInfo info;
    info.paramIndex = index;

    const auto normalised = [&info](const char* id, const char* name, float def) {
        info.stableId = id;
        info.name = name;
        info.minValue = 0.0f;
        info.maxValue = 1.0f;
        info.defaultValue = def;
        info.displayFormat = DisplayFormat::Percent;
    };

    switch (index) {
        case MutableRingsPlugin::kStructure:
            normalised("structure", "Structure", 0.25f);
            break;

        case MutableRingsPlugin::kBrightness:
            normalised("brightness", "Brightness", 0.5f);
            break;

        case MutableRingsPlugin::kDamping:
            normalised("damping", "Damping", 0.5f);
            break;

        case MutableRingsPlugin::kPosition:
            normalised("position", "Position", 0.25f);
            break;

        case MutableRingsPlugin::kModel:
            info.stableId = "model";
            info.name = "Model";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = static_cast<float>(kNumModels - 1);
            info.defaultValue = 0.0f;
            info.choices = {"Modal", "Sympathetic", "String", "FM", "Sym Quant", "String+Verb"};
            break;

        case MutableRingsPlugin::kPolyphony:
            info.stableId = "polyphony";
            info.name = "Polyphony";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 1.0f;  // index 1 -> 2 voices
            info.choices = {"1", "2", "4"};
            break;

        case MutableRingsPlugin::kChord:
            info.stableId = "chord";
            info.name = "Chord";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.0f;
            info.choices = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
            break;

        case MutableRingsPlugin::kPitch:
            info.stableId = "pitch";
            info.name = "Pitch";
            info.unit = "st";
            info.minValue = -24.0f;
            info.maxValue = 24.0f;
            info.defaultValue = 0.0f;
            break;

        case MutableRingsPlugin::kFine:
            info.stableId = "fine";
            info.name = "Fine";
            info.unit = "ct";
            info.minValue = -100.0f;
            info.maxValue = 100.0f;
            info.defaultValue = 0.0f;
            break;

        case MutableRingsPlugin::kLevel:
            info.stableId = "level";
            info.name = "Level";
            info.unit = "dB";
            // The retired host-native plugin's NormalisableRange carried skew
            // 4, which JUCE applies as real = min + span * normalized^(1/skew).
            info.scale = ParameterScale::Exponential;
            info.skewFactor = 0.25f;
            info.minValue = -60.0f;
            info.maxValue = 12.0f;
            info.defaultValue = 0.0f;
            break;

        default:
            break;
    }

    return info;
}

// 4-point, 3rd-order Hermite (Catmull-Rom) interpolation between x[1] and x[2].
inline float cubic(const float* x, float t) {
    const float a = -0.5f * x[0] + 1.5f * x[1] - 1.5f * x[2] + 0.5f * x[3];
    const float b = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    const float c = -0.5f * x[0] + 0.5f * x[2];
    const float d = x[1];
    return ((a * t + b) * t + c) * t + d;
}

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
        pendingCount_ = 0;
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
    //
    // Queued rather than applied here, because the strum is consumed once per
    // internal block: a chord arrives as note-ons at one timestamp, and writing
    // the note straight in left only the last of them a string (#2364).
    void noteOn(int n) {
        if (pendingCount_ < kMaxPendingNotes)
            pendingNotes_[static_cast<size_t>(pendingCount_++)] = n;
    }

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
    inline void nextSource(float& l, float& r) {
        if (chunkPos_ >= kBlock) {
            // One queued note per block, which is the rate Rings strums at.
            bool strum = false;
            if (pendingCount_ > 0) {
                currentNote_ = pendingNotes_[0];
                for (int i = 1; i < pendingCount_; ++i)
                    pendingNotes_[static_cast<size_t>(i - 1)] =
                        pendingNotes_[static_cast<size_t>(i)];
                --pendingCount_;
                strum = true;
            }

            rings::PerformanceState ps{};
            ps.internal_exciter = true;  // internal plucker; no audio input
            ps.internal_strum = false;   // we drive strum from MIDI
            ps.internal_note = false;    // we supply the note
            ps.strum = strum;
            ps.tonic = 0.0f;
            ps.fm = 0.0f;
            ps.note = static_cast<float>(currentNote_) + transpose_;
            ps.chord = chord_;
            part_.Process(ps, patch_, silence_, out_, aux_, static_cast<size_t>(kBlock));
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

    /// Note-ons waiting for an internal block to strum them. A chord is four
    /// voices at most, and a queue several times that outlives any host block.
    static constexpr int kMaxPendingNotes = 16;
    std::array<int, kMaxPendingNotes> pendingNotes_{};
    int pendingCount_ = 0;
    int currentNote_ = 60;
    int chord_ = 0;
    float transpose_ = 0.0f;
    int appliedPolyphony_ = -1;
};

//==============================================================================
const char* MutableRingsPlugin::xmlTypeName = "magda_rings";

MutableRingsPlugin::MutableRingsPlugin() : impl_(std::make_unique<Impl>()) {
    for (int index = 0; index < kNumParams; ++index) {
        const auto info = slotInfo(index);
        domains_[static_cast<size_t>(index)] = ParameterUtils::domainOf(info);
        values_[static_cast<size_t>(index)] =
            ParameterUtils::realToNormalized(info.defaultValue, info);
    }
}

MutableRingsPlugin::~MutableRingsPlugin() = default;

ParameterInfo MutableRingsPlugin::parameterInfo(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float MutableRingsPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

void MutableRingsPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);
}

float MutableRingsPlugin::displayValue(int index) const {
    return ParameterUtils::normalizedToReal(values_[static_cast<size_t>(index)],
                                            domains_[static_cast<size_t>(index)]);
}

void MutableRingsPlugin::prepare(const DevicePrepareContext& context) {
    sampleRate_ = context.sampleRate;
    impl_->prepare(sampleRate_);
    scratch_.setSize(2, std::max(context.maximumBlockSize, 1), false, true, false);
}

void MutableRingsPlugin::reset() {
    impl_->resetVoice();
}

void MutableRingsPlugin::process(DeviceProcessContext& context) {
    if (context.audio == nullptr || context.numSamples <= 0)
        return;

    rings::Patch patch;
    patch.structure = displayValue(kStructure);
    patch.brightness = displayValue(kBrightness);
    patch.damping = displayValue(kDamping);
    patch.position = displayValue(kPosition);

    const int model = juce::jlimit(0, kNumModels - 1, juce::roundToInt(displayValue(kModel)));
    const int polyphony = 1 << juce::jlimit(0, 2, juce::roundToInt(displayValue(kPolyphony)));
    const int chord = juce::jlimit(0, 10, juce::roundToInt(displayValue(kChord)));
    const float transpose = displayValue(kPitch) + displayValue(kFine) * 0.01f;
    impl_->configure(patch, model, polyphony, chord, transpose);

    auto& buffer = *context.audio;
    const int start = context.startSample;
    auto* destL = scratch_.getWritePointer(0);
    auto* destR = buffer.getNumChannels() > 1 ? scratch_.getWritePointer(1) : nullptr;

    int pos = 0;
    auto renderTo = [&](int upto) {
        if (upto <= pos)
            return;
        impl_->generate(destL + pos, destR != nullptr ? destR + pos : nullptr, upto - pos);
        pos = upto;
    };

    if (context.midiIn != nullptr) {
        for (int eventIndex = 0; eventIndex < context.midiIn->size(); ++eventIndex) {
            const auto& m = context.midiIn->message(eventIndex);
            if (!m.isNoteOn() || m.getVelocity() == 0)
                continue;  // Rings has no note-off gate; resonators decay via Damping
            const int evPos = juce::jlimit(0, context.numSamples - 1,
                                           juce::roundToInt(m.getTimeStamp() * sampleRate_));
            renderTo(evPos);
            impl_->noteOn(m.getNoteNumber());
        }
    }
    renderTo(context.numSamples);

    // Add rather than replace (#2370): on the TE leg the buffer may already
    // carry an audio clip's signal that must not be clobbered. The native
    // engine clears an instrument's channels before running it, so add is a
    // no-op difference there.
    const float gain = juce::Decibels::decibelsToGain(displayValue(kLevel));
    buffer.addFrom(0, start, scratch_, 0, 0, context.numSamples, gain);
    if (destR != nullptr)
        buffer.addFrom(1, start, scratch_, 1, 0, context.numSamples, gain);
}

}  // namespace magda::daw::audio
