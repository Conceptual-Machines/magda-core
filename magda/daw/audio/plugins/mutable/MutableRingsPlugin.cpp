// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/mutable/MutableRingsPlugin.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "plugins/mutable/MutableRateHarness.hpp"

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable),
// compiled with -DTEST. Walled behind the pimpl so this is the only TU in the
// base device pack that sees the eurorack headers.
#include "rings/dsp/dsp.h"
#include "rings/dsp/part.h"

namespace magda::daw::audio {

namespace {

constexpr int kBlock = static_cast<int>(rings::kMaxBlockSize);  // 24
constexpr double kInternalRate = MutableRingsPlugin::kNativeRate;

constexpr int kNumModels = 6;

/// Where the tail was measured along damping.
constexpr float kTailDamping[] = {0.0f, 0.3f, 0.5f, 0.6f, 0.7f, 0.8f, 0.85f, 0.9f, 0.95f};

/** @brief @p y, measured at @p x, read at @p at: flat past either end. */
template <size_t N> double piecewise(const float (&x)[N], const double (&y)[N], float at) {
    if (at <= x[0])
        return y[0];
    for (size_t i = 1; i < N; ++i)
        if (at <= x[i])
            return y[i - 1] + (at - x[i - 1]) / (x[i] - x[i - 1]) * (y[i] - y[i - 1]);
    return y[N - 1];
}

/// One slot's metadata. The ids, order and display ranges are pinned to what
/// the retired host-native plugin registered, because projects store parameter
/// values in the model in display units against these ranges and address the
/// slots by index.
sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    info.index = index;

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
            info.choices = sdk::choicesFromLabels(
                {"Modal", "Sympathetic", "String", "FM", "Sym Quant", "String+Verb"});
            break;

        case MutableRingsPlugin::kPolyphony:
            info.stableId = "polyphony";
            info.name = "Polyphony";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 2.0f;
            info.defaultValue = 1.0f;  // index 1 -> 2 voices
            info.choices = sdk::choicesFromLabels({"1", "2", "4"});
            break;

        case MutableRingsPlugin::kChord:
            info.stableId = "chord";
            info.name = "Chord";
            info.scale = ParameterScale::Discrete;
            info.minValue = 0.0f;
            info.maxValue = 10.0f;
            info.defaultValue = 0.0f;
            info.choices =
                sdk::choicesFromLabels({"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
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
            info.exponent = 0.25f;
            info.minValue = -60.0f;
            info.maxValue = 12.0f;
            info.defaultValue = 0.0f;
            break;

        default:
            break;
    }

    return info;
}

}  // namespace

//==============================================================================
struct MutableRingsPlugin::Impl {
    Impl() {
        jassert(static_cast<double>(rings::kSampleRate) == kInternalRate);
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
        harness_.prepare(hostRate);
        resetVoice();
    }

    int latencySamples() const {
        return harness_.latencySamples();
    }

    void resetVoice() {
        harness_.reset();
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
        harness_.render(outL, outR, n,
                        [this](float* l, float* r, int frames) { renderBlock(l, r, frames); });
    }

  private:
    void renderBlock(float* l, float* r, int frames) {
        // One queued note per block, which is the rate Rings strums at.
        bool strum = false;
        if (pendingCount_ > 0) {
            currentNote_ = pendingNotes_[0];
            for (int i = 1; i < pendingCount_; ++i)
                pendingNotes_[static_cast<size_t>(i - 1)] = pendingNotes_[static_cast<size_t>(i)];
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
        part_.Process(ps, patch_, silence_, l, r, static_cast<size_t>(frames));
    }

    rings::Part part_;
    rings::Patch patch_{};
    uint16_t reverbBuffer_[32768];

    float silence_[kBlock];
    MutableRateHarness harness_{kInternalRate, kBlock, MutableRateHarness::Role::Instrument};

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
            ParameterUtils::realToNormalized(info.defaultValue, ParameterUtils::domainOf(info));
    }
}

MutableRingsPlugin::~MutableRingsPlugin() = default;

sdk::ParameterDescriptor MutableRingsPlugin::parameterDescriptor(int index) const {
    if (index < 0 || index >= kNumParams)
        return {};
    return slotInfo(index);
}

float MutableRingsPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

double MutableRingsPlugin::tailSecondsFor(int model, float damping, float brightness,
                                          float levelDb) {
    // Strum to the last sample above -60 dBFS, worst of notes 24 to 120, times the model's worst
    // structure (up to 1.77x, the string), +20% headroom, held non-decreasing. Brightness 1 rings
    // up to 6x longer than 0.5, the modal bank's notes 96 to 108 most, and less than 0.5 rings
    // shorter. The strings go infinite from damping 0.95.
    constexpr int kPoints = static_cast<int>(std::size(kTailDamping));
    constexpr double kSeconds[kNumModels][2][kPoints] = {
        {{0.4, 0.4, 1.4, 3.5, 8.8, 22.2, 35.1, 60.0, 60.0},
         {0.4, 1.3, 8.0, 20.1, 50.5, 60.0, 60.0, 60.0, 60.0}},  // modal
        {{4.2, 12.0, 15.8, 17.9, 17.9, 18.8, 19.8, 21.1, 60.0},
         {6.9, 14.0, 15.8, 19.3, 19.3, 20.0, 21.4, 25.0, 60.0}},  // sympathetic
        {{0.0, 0.9, 2.9, 5.0, 9.3, 10.6, 13.7, 15.9, 60.0},
         {0.1, 1.3, 4.7, 8.5, 13.2, 16.4, 19.0, 23.1, 60.0}},  // string
        {{0.1, 0.5, 1.6, 2.7, 4.8, 8.3, 10.9, 14.4, 60.0},
         {0.1, 0.5, 1.6, 2.7, 4.8, 8.3, 10.9, 14.4, 60.0}},  // FM
        {{4.6, 11.5, 12.8, 17.9, 17.9, 21.5, 21.5, 21.5, 60.0},
         {4.6, 11.5, 21.8, 22.0, 22.0, 22.0, 22.7, 23.7, 60.0}},  // quantised sympathetic
        {{0.2, 2.8, 4.9, 11.9, 12.1, 17.4, 45.2, 60.0, 60.0},
         {0.6, 3.1, 11.2, 15.3, 17.4, 21.8, 48.3, 60.0, 60.0}},  // string and reverb
    };

    const auto& rows = kSeconds[juce::jlimit(0, kNumModels - 1, model)];
    const double dull = piecewise(kTailDamping, rows[0], damping);
    const double bright = piecewise(kTailDamping, rows[1], damping);
    // Convex in brightness, measured at 0.62, 0.75 and 0.87, so the line over-declares.
    double seconds = dull + juce::jlimit(0.0f, 1.0f, 2.0f * brightness - 1.0f) * (bright - dull);

    // Gain lifts the decay over the -60 dBFS line: +12 dB measured up to 1.72x, the string and
    // reverb at damping 0.5.
    seconds *= 1.0 + 0.06 * std::max(0.0f, levelDb);

    return juce::jlimit(0.25, kMaxTailSeconds, seconds);
}

void MutableRingsPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);

    if (index == kModel || index == kDamping || index == kBrightness || index == kLevel)
        tailSeconds_.store(tailSecondsFor(juce::roundToInt(displayValue(kModel)),
                                          displayValue(kDamping), displayValue(kBrightness),
                                          displayValue(kLevel)),
                           std::memory_order_relaxed);
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

int MutableRingsPlugin::latencySamples() const {
    return impl_->latencySamples();
}

void MutableRingsPlugin::process(DeviceProcessContext& context) {
    const int numSamples = context.numSamples();
    if (numSamples <= 0)
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

    auto buffer = juceAudio(context);
    const int start = 0;
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
        const DeviceMidiInput in(*context.midiIn, sampleRate_);
        for (int eventIndex = 0; eventIndex < in.size(); ++eventIndex) {
            const auto m = in.message(eventIndex);
            if (!m.isNoteOn() || m.getVelocity() == 0)
                continue;  // Rings has no note-off gate; resonators decay via Damping
            const int evPos = juce::jlimit(0, numSamples - 1,
                                           midiEventPosition(m.getTimeStamp(), sampleRate_).sample);
            renderTo(evPos);
            impl_->noteOn(m.getNoteNumber());
        }
    }
    renderTo(numSamples);

    // Add rather than replace (#2370): the buffer may already carry another
    // signal that must not be clobbered.
    const float gain = juce::Decibels::decibelsToGain(displayValue(kLevel));
    buffer.addFrom(0, start, scratch_, 0, 0, numSamples, gain);
    if (destR != nullptr)
        buffer.addFrom(1, start, scratch_, 1, 0, numSamples, gain);
}

}  // namespace magda::daw::audio
