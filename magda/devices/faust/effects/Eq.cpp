#include "devices/faust/effects/Eq.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace magda::devices::faust {

namespace {

// Bands default to disabled Bell filters. That keeps the inserted EQ neutral
// and avoids spending per-sample CPU on inactive biquads.
struct BandDefaults {
    float enabled;
    float type;
    float freq;
    float q;
};
constexpr BandDefaults kBandDefaults[Eq::kBandCount] = {
    {0.0f, 2.0f, 30.0f, 1.0f},    {0.0f, 2.0f, 100.0f, 1.0f},   {0.0f, 2.0f, 250.0f, 1.0f},
    {0.0f, 2.0f, 800.0f, 1.0f},   {0.0f, 2.0f, 2000.0f, 1.0f},  {0.0f, 2.0f, 5000.0f, 1.0f},
    {0.0f, 2.0f, 10000.0f, 1.0f}, {0.0f, 2.0f, 18000.0f, 1.0f},
};

constexpr float kTwoPi = 6.28318530717958647692f;

struct RbjCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

RbjCoeffs makeRbj(Eq::BandType type, float f0, float gainDb, float q, float sampleRate) {
    RbjCoeffs out;
    const float safeQ = std::max(0.05f, q);
    const float fc = std::clamp(f0, 1.0f, sampleRate * 0.45f);
    const float w0 = kTwoPi * fc / sampleRate;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * safeQ);

    auto normalise = [&out](float b0, float b1, float b2, float a0, float a1, float a2) {
        const float inv = 1.0f / a0;
        out.b0 = b0 * inv;
        out.b1 = b1 * inv;
        out.b2 = b2 * inv;
        out.a1 = a1 * inv;
        out.a2 = a2 * inv;
    };

    using BandType = Eq::BandType;
    switch (type) {
        case BandType::Highpass:
            normalise((1.0f + cw) * 0.5f, -(1.0f + cw), (1.0f + cw) * 0.5f, 1.0f + alpha,
                      -2.0f * cw, 1.0f - alpha);
            break;
        case BandType::Lowpass:
            normalise((1.0f - cw) * 0.5f, 1.0f - cw, (1.0f - cw) * 0.5f, 1.0f + alpha, -2.0f * cw,
                      1.0f - alpha);
            break;
        case BandType::Bell: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            normalise(1.0f + alpha * A, -2.0f * cw, 1.0f - alpha * A, 1.0f + alpha / A, -2.0f * cw,
                      1.0f - alpha / A);
            break;
        }
        case BandType::LowShelf: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float shelfAlpha = sw / 1.41421356f;
            const float sqA2 = 2.0f * std::sqrt(A) * shelfAlpha;
            normalise(A * ((A + 1.0f) - (A - 1.0f) * cw + sqA2),
                      2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw),
                      A * ((A + 1.0f) - (A - 1.0f) * cw - sqA2),
                      (A + 1.0f) + (A - 1.0f) * cw + sqA2, -2.0f * ((A - 1.0f) + (A + 1.0f) * cw),
                      (A + 1.0f) + (A - 1.0f) * cw - sqA2);
            break;
        }
        case BandType::HighShelf: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float shelfAlpha = sw / 1.41421356f;
            const float sqA2 = 2.0f * std::sqrt(A) * shelfAlpha;
            normalise(A * ((A + 1.0f) + (A - 1.0f) * cw + sqA2),
                      -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw),
                      A * ((A + 1.0f) + (A - 1.0f) * cw - sqA2),
                      (A + 1.0f) - (A - 1.0f) * cw + sqA2, 2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
                      (A + 1.0f) - (A - 1.0f) * cw - sqA2);
            break;
        }
        case BandType::Notch:
            normalise(1.0f, -2.0f * cw, 1.0f, 1.0f + alpha, -2.0f * cw, 1.0f - alpha);
            break;
    }
    return out;
}

float processRbj(float x, const RbjCoeffs& c, Eq::BiquadState& s) {
    const float y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    return y;
}

// Identifier-safe band name used in state ids.
std::string bandIdPrefix(int band) {
    return "band" + std::to_string(band + 1);
}

std::string bandDisplayPrefix(int band) {
    return "Band " + std::to_string(band + 1);
}

}  // namespace

Eq::Eq() {
    initEffect();
}

std::vector<SlotInfo> Eq::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    // Per-band slots.
    for (int band = 0; band < kBandCount; ++band) {
        const auto& defaults = kBandDefaults[band];
        const std::string prefix = bandDisplayPrefix(band);

        const int enabledSlot = bandSlot(band, kBandEnabledOffset);
        infos[enabledSlot] = {.name = prefix + " Enabled",
                              .scale = sdk::ParameterScale::Boolean,
                              .minValue = 0.0f,
                              .maxValue = 1.0f,
                              .defaultValue = defaults.enabled};

        const int typeSlot = bandSlot(band, kBandTypeOffset);
        infos[typeSlot] = {.name = prefix + " Type",
                           .scale = sdk::ParameterScale::Discrete,
                           .minValue = 0.0f,
                           .maxValue = static_cast<float>(kBandTypeCount - 1),
                           .defaultValue = defaults.type,
                           .choices = {"HP", "LowShelf", "Bell", "HighShelf", "LP", "Notch"}};

        const int freqSlot = bandSlot(band, kBandFreqOffset);
        infos[freqSlot] = {.name = prefix + " Freq",
                           .unit = "Hz",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 20.0f,
                           .maxValue = 20000.0f,
                           .defaultValue = defaults.freq,
                           .scaleAnchor = 1000.0f};

        const int gainSlot = bandSlot(band, kBandGainOffset);
        infos[gainSlot] = {.name = prefix + " Gain",
                           .unit = "dB",
                           .scale = sdk::ParameterScale::Linear,
                           .minValue = -24.0f,
                           .maxValue = 24.0f,
                           .defaultValue = 0.0f};

        const int qSlot = bandSlot(band, kBandQOffset);
        infos[qSlot] = {.name = prefix + " Q",
                        .scale = sdk::ParameterScale::Logarithmic,
                        .minValue = 0.1f,
                        .maxValue = 10.0f,
                        .defaultValue = defaults.q,
                        .scaleAnchor = 1.0f};
    }

    infos[kOutputSlot] = {.name = "Output",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    return infos;
}

std::string Eq::slotId(int slotIndex) const {
    // Pinned: these ids key saved state, and the default scheme would not make
    // "band3_filter_type" of "Band 3 Type".
    if (slotIndex < 0 || slotIndex > kOutputSlot)
        return {};
    if (slotIndex == kOutputSlot)
        return "magda_eq_output";

    static const char* kRoleSuffix[kSlotsPerBand] = {"enabled", "filter_type", "freq", "gain", "q"};
    return "magda_eq_" + bandIdPrefix(slotIndex / kSlotsPerBand) + "_" +
           kRoleSuffix[slotIndex % kSlotsPerBand];
}

sdk::RestoreResult Eq::restoreState(const sdk::StateNode& state) {
    curveCollapsed_ = state.getBool(kCurveCollapsedKey, true);
    return sdk::RestoreResult::success();
}

void Eq::setCurveCollapsed(bool collapsed) {
    curveCollapsed_ = collapsed;

    // The toggle is the device's own state: the model's document has no other way to learn it.
    if (auto* reportTo = host()) {
        sdk::StateNode state;
        state.setBool(kCurveCollapsedKey, collapsed);
        reportTo->stateChanged(std::move(state));
    }
}

Eq::BandSnapshot Eq::getBandSnapshot(int band) const {
    BandSnapshot snapshot;
    if (band < 0 || band >= kBandCount)
        return snapshot;

    snapshot.enabled = slotDisplayValue(bandSlot(band, kBandEnabledOffset)) >= 0.5f;
    const int typeIndex =
        std::clamp(static_cast<int>(std::lround(slotDisplayValue(bandSlot(band, kBandTypeOffset)))),
                   0, kBandTypeCount - 1);
    snapshot.type = static_cast<BandType>(typeIndex);
    snapshot.freq = slotDisplayValue(bandSlot(band, kBandFreqOffset));
    snapshot.gainDb = slotDisplayValue(bandSlot(band, kBandGainOffset));
    snapshot.q = slotDisplayValue(bandSlot(band, kBandQOffset));
    return snapshot;
}

void Eq::onPrepare(double, int maximumBlockSize) {
    for (auto& bandStates : biquadStates_)
        bandStates.clear();
}

void Eq::onRelease() {
    for (auto& bandStates : biquadStates_)
        bandStates.clear();
}

void Eq::onReset() {
    for (auto& bandStates : biquadStates_)
        for (auto& state : bandStates)
            state = {};
}

void Eq::processAudio(sdk::ProcessContext& context) {
    // No Faust engine: RBJ biquads, whose coefficient maths the curve view shares.
    const int numSamples = context.numSamples();
    const int hostChannels = context.audio.numChannels();
    if (hostChannels <= 0)
        return;

    for (auto& bandStates : biquadStates_)
        if (static_cast<int>(bandStates.size()) < hostChannels)
            bandStates.resize(static_cast<size_t>(hostChannels));

    const auto sampleRate = static_cast<float>(currentSampleRate());
    std::array<bool, kBandCount> bandEnabled{};
    std::array<RbjCoeffs, kBandCount> coeffs{};
    for (int band = 0; band < kBandCount; ++band) {
        const auto snapshot = getBandSnapshot(band);
        bandEnabled[static_cast<size_t>(band)] = snapshot.enabled;
        if (!snapshot.enabled) {
            auto& states = biquadStates_[static_cast<size_t>(band)];
            std::fill(states.begin(), states.end(), BiquadState{});
            continue;
        }
        coeffs[static_cast<size_t>(band)] =
            makeRbj(snapshot.type, snapshot.freq, snapshot.gainDb, snapshot.q, sampleRate);
    }
    const float outputGain = std::pow(10.0f, slotDisplayValue(kOutputSlot) / 20.0f);

    preSpectrumTap_.writeDownmix(context.audio);

    // A fast high-Q sweep can go non-finite, hence the sanitising pass. The enabled bands are
    // compacted once per block.
    std::array<int, kBandCount> activeBands{};
    int activeBandCount = 0;
    for (int band = 0; band < kBandCount; ++band)
        if (bandEnabled[static_cast<size_t>(band)])
            activeBands[static_cast<size_t>(activeBandCount++)] = band;

    for (int channel = 0; channel < hostChannels; ++channel) {
        float* out = context.audio.channel(channel);
        for (int i = 0; i < numSamples; ++i) {
            float sample = out[i];
            for (int active = 0; active < activeBandCount; ++active) {
                const auto band = static_cast<size_t>(activeBands[static_cast<size_t>(active)]);
                sample = processRbj(sample, coeffs[band],
                                    biquadStates_[band][static_cast<size_t>(channel)]);
            }
            out[i] = sanitise(sample * outputGain);
        }
    }
    postSpectrumTap_.writeDownmix(context.audio);
}

}  // namespace magda::devices::faust
