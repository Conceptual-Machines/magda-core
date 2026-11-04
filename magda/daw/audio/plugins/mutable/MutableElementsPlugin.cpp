// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/mutable/MutableElementsPlugin.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "plugins/mutable/MutableRateHarness.hpp"

// Upstream Mutable Instruments DSP (third_party/eurorack, magda::mutable).
// Compiled with -DTEST (set on the magda_mutable target) so stmlib uses its
// portable code paths. Walled behind the pimpl so this is the only TU in the
// base device pack that sees the eurorack headers.
#include "elements/dsp/dsp.h"
#include "elements/dsp/part.h"

namespace magda::daw::audio {

namespace {

constexpr int kBlock = static_cast<int>(elements::kMaxBlockSize);  // 16
constexpr double kInternalRate = MutableElementsPlugin::kNativeRate;

/** @brief @p y, measured at @p x, read at @p at: flat past either end. */
template <size_t N> double piecewise(const float (&x)[N], const double (&y)[N], float at) {
    if (at <= x[0])
        return y[0];
    for (size_t i = 1; i < N; ++i)
        if (at <= x[i])
            return y[i - 1] + (at - x[i - 1]) / (x[i] - x[i - 1]) * (y[i] - y[i - 1]);
    return y[N - 1];
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
sdk::ParameterDescriptor slotInfo(int index) {
    sdk::ParameterDescriptor info;
    if (index < 0 || index >= MutableElementsPlugin::kNumParams)
        return info;

    const auto& d = kDescs[static_cast<size_t>(index)];
    info.index = index;
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
            info.exponent = 0.25f;
            info.minValue = -60.0f;
            info.maxValue = 12.0f;
            break;
    }

    return info;
}

}  // namespace

//==============================================================================
/** @brief The Mutable DSP voice, its rate crossing and the monophonic MIDI state. */
struct MutableElementsPlugin::Impl {
    Impl() {
        jassert(static_cast<double>(elements::kSampleRate) == kInternalRate);
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
        harness_.prepare(hostRate);
        resetVoice();
    }

    int latencySamples() const {
        return harness_.latencySamples();
    }

    void resetVoice() {
        harness_.reset();
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

    /** @brief Render @p n host-rate samples, @p outR null for mono. */
    void generate(float* outL, float* outR, int n) {
        harness_.render(outL, outR, n, [this](float* l, float* r, int frames) {
            part_.Process(perf_, silence_, silence_, l, r, static_cast<size_t>(frames));
        });
    }

  private:
    elements::Part part_;
    elements::PerformanceState perf_{};
    uint16_t reverbBuffer_[32768];

    float silence_[kBlock];
    MutableRateHarness harness_{kInternalRate, kBlock, MutableRateHarness::Role::Instrument};

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
            ParameterUtils::realToNormalized(info.defaultValue, ParameterUtils::domainOf(info));
    }
}

MutableElementsPlugin::~MutableElementsPlugin() = default;

sdk::ParameterDescriptor MutableElementsPlugin::parameterDescriptor(int index) const {
    return slotInfo(index);
}

float MutableElementsPlugin::parameterValue(int index) const {
    if (index < 0 || index >= kNumParams)
        return 0.0f;
    return values_[static_cast<size_t>(index)];
}

double MutableElementsPlugin::tailSecondsFor(float damping, float brightness, float space,
                                             float levelDb) {
    // Release to the last sample above -60 dBFS, worst of notes 24 to 108, +25% headroom, which
    // also covers geometry, position and the exciters (within 8%). The resonator in the space mix
    // at 0.3, its loudest; brightness 1 rings up to 5x longer than 0.5, and less than 0.5 rings
    // shorter.
    constexpr float kDamping[] = {0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.85f, 0.9f, 0.95f, 1.0f};
    constexpr double kDull[] = {0.4, 0.4, 1.2, 2.8, 6.1, 8.9, 12.9, 18.9, 29.4};
    constexpr double kBright[] = {0.4, 2.9, 6.3, 13.6, 28.7, 41.8, 60.0, 60.0, 60.0};

    // The reverb on its own, at damping 0: it only engages past space 0.6.
    constexpr float kSpace[] = {0.6f, 0.65f, 0.7f, 0.8f, 0.9f, 1.0f};
    constexpr double kReverb[] = {0.0, 0.2, 0.8, 1.6, 3.5, 8.1};

    const double dull = piecewise(kDamping, kDull, damping);
    const double bright = piecewise(kDamping, kBright, damping);
    // Convex in brightness, measured at 0.62, 0.75 and 0.87, so the line over-declares.
    double seconds = dull + juce::jlimit(0.0f, 1.0f, 2.0f * brightness - 1.0f) * (bright - dull);

    // Summed, because the reverb rings on what the resonator hands it.
    seconds += piecewise(kSpace, kReverb, space);

    // Gain lifts the decay over the -60 dBFS line: +12 dB measured up to 1.34x.
    seconds *= 1.0 + 0.03 * std::max(0.0f, levelDb);

    return juce::jlimit(0.25, kMaxTailSeconds, seconds);
}

void MutableElementsPlugin::setParameterValue(int index, float value) {
    if (index < 0 || index >= kNumParams)
        return;
    values_[static_cast<size_t>(index)] = juce::jlimit(0.0f, 1.0f, value);

    if (index == kDamping || index == kBrightness || index == kSpace || index == kLevel)
        tailSeconds_.store(tailSecondsFor(displayValue(kDamping), displayValue(kBrightness),
                                          displayValue(kSpace), displayValue(kLevel)),
                           std::memory_order_relaxed);
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

int MutableElementsPlugin::latencySamples() const {
    return impl_->latencySamples();
}

void MutableElementsPlugin::process(DeviceProcessContext& context) {
    const int numSamples = context.numSamples();
    if (numSamples <= 0)
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

    auto buffer = juceAudio(context);
    const int start = 0;
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

    if (context.midiIn != nullptr) {
        const DeviceMidiInput in(*context.midiIn, sampleRate_);

        // The host's panic travels beside the events rather than as CC 123 (#2418).
        if (in.isAllNotesOff())
            impl_->allNotesOff();

        for (int eventIndex = 0; eventIndex < in.size(); ++eventIndex) {
            const auto m = in.message(eventIndex);
            const bool isPanic = m.isController() &&
                                 (m.getControllerNumber() == 120 || m.getControllerNumber() == 123);
            if (!m.isNoteOn() && !m.isNoteOff() && !isPanic)
                continue;
            const int evPos = juce::jlimit(0, numSamples - 1,
                                           midiEventPosition(m.getTimeStamp(), sampleRate_).sample);
            renderTo(evPos);
            if (isPanic)
                impl_->allNotesOff();  // CC120/123: release any stuck gate
            else if (m.isNoteOn() && m.getVelocity() > 0)
                impl_->noteOn(m.getNoteNumber(), m.getFloatVelocity());
            else
                impl_->noteOff(m.getNoteNumber());
        }
    }
    renderTo(numSamples);

    // Add rather than replace (#2370): the buffer may already carry another
    // signal that must not be clobbered.
    const float gain = juce::Decibels::decibelsToGain(v[kLevel]);
    buffer.addFrom(0, start, scratch_, 0, 0, numSamples, gain);
    if (destR != nullptr)
        buffer.addFrom(1, start, scratch_, 1, 0, numSamples, gain);
}

}  // namespace magda::daw::audio
