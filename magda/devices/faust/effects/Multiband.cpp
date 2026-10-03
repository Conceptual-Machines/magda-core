#include "devices/faust/effects/Multiband.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace magda::devices::faust {

namespace {

constexpr float kMinRatio = 0.05f;
constexpr float kMaxRatio = 100.0f;
constexpr float kMinLevelDb = -100.0f;
constexpr double kBiquadQ = 0.7071067811865476;

float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float gainToDb(float gain) {
    return 20.0f * std::log10(std::max(gain, 1.0e-5f));
}

float coefficientForMs(float ms, double sampleRate) {
    return std::exp(-1.0f / (std::max(0.01f, ms) * 0.001f * static_cast<float>(sampleRate)));
}

float ratioSlope(float ratio) {
    return 1.0f / std::clamp(ratio, kMinRatio, kMaxRatio);
}

float dynamicsGainDb(float levelDb, float lowerThresholdDb, float upperThresholdDb,
                     float belowRatio, float aboveRatio, float rangeDb, float amount) {
    const float lower = std::min(lowerThresholdDb, upperThresholdDb);
    const float upper = std::max(lowerThresholdDb, upperThresholdDb);

    float anchorDb = 0.0f;
    float ratio = 1.0f;
    if (levelDb < lower) {
        anchorDb = lower;
        ratio = belowRatio;
    } else if (levelDb > upper) {
        anchorDb = upper;
        ratio = aboveRatio;
    } else {
        return 0.0f;
    }

    const float targetLevelDb = anchorDb + (levelDb - anchorDb) * ratioSlope(ratio);
    const float unclampedGainDb = targetLevelDb - levelDb;
    return std::clamp(unclampedGainDb, -rangeDb, rangeDb) * amount;
}

}  // namespace

Multiband::Multiband() {
    initEffect();
}

std::vector<SlotInfo> Multiband::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kAmountSlot] = {.name = "Amount",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 0.8f};
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = "ms",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 100.0f,
                          .defaultValue = 3.0f,
                          .scaleAnchor = 10.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = "ms",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 5.0f,
                           .maxValue = 1000.0f,
                           .defaultValue = 120.0f,
                           .scaleAnchor = 100.0f};
    infos[kInputSlot] = {.name = "Input",
                         .unit = "dB",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = -24.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 24.0f,
                          .defaultValue = 0.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    auto setGain = [&infos](int slot, std::string name) {
        infos[slot] = {.name = std::move(name),
                       .unit = "dB",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = -24.0f,
                       .maxValue = 24.0f,
                       .defaultValue = 0.0f};
    };
    setGain(kLowInputSlot, "Low Input");
    setGain(kMidInputSlot, "Mid Input");
    setGain(kHighInputSlot, "High Input");
    setGain(kLowGainSlot, "Low Output");
    setGain(kMidGainSlot, "Mid Output");
    setGain(kHighGainSlot, "High Output");

    auto setThreshold = [&infos](int slot, std::string name, float defaultValue) {
        infos[slot] = {.name = std::move(name),
                       .unit = "dB",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = -80.0f,
                       .maxValue = 0.0f,
                       .defaultValue = defaultValue};
    };
    auto setRatio = [&infos](int slot, std::string name) {
        infos[slot] = {.name = std::move(name),
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.05f,
                       .maxValue = kMaxRatio,
                       .defaultValue = 8.0f};
    };
    auto setTiming = [&infos](int attackSlot, int releaseSlot, const std::string& bandName) {
        infos[attackSlot] = {.name = bandName + " Attack",
                             .unit = "ms",
                             .scale = sdk::ParameterScale::Logarithmic,
                             .minValue = 0.1f,
                             .maxValue = 100.0f,
                             .defaultValue = 3.0f,
                             .scaleAnchor = 10.0f};
        infos[releaseSlot] = {.name = bandName + " Release",
                              .unit = "ms",
                              .scale = sdk::ParameterScale::Logarithmic,
                              .minValue = 5.0f,
                              .maxValue = 1000.0f,
                              .defaultValue = 120.0f,
                              .scaleAnchor = 100.0f};
    };
    auto setBand = [&infos, setThreshold,
                    setRatio](int lowerThresholdSlot, int upperThresholdSlot, int belowRatioSlot,
                              int aboveRatioSlot, int rangeSlot, int limitSlot,
                              std::string bandName, float lowerDefault, float upperDefault) {
        setThreshold(lowerThresholdSlot, bandName + " Lower Threshold", lowerDefault);
        setThreshold(upperThresholdSlot, bandName + " Upper Threshold", upperDefault);
        setRatio(belowRatioSlot, bandName + " Below Ratio");
        setRatio(aboveRatioSlot, bandName + " Above Ratio");
        infos[rangeSlot] = {.name = bandName + " Range",
                            .unit = "dB",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 48.0f,
                            .defaultValue = 24.0f};
        infos[limitSlot] = {.name = bandName + " Limit",
                            .unit = "dB",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = -24.0f,
                            .maxValue = 12.0f,
                            .defaultValue = 0.0f};
    };

    setBand(kLowLowerThresholdSlot, kLowUpperThresholdSlot, kLowBelowRatioSlot, kLowAboveRatioSlot,
            kLowRangeSlot, kLowLimitSlot, "Low", -48.0f, -24.0f);
    setTiming(kLowAttackSlot, kLowReleaseSlot, "Low");
    setBand(kMidLowerThresholdSlot, kMidUpperThresholdSlot, kMidBelowRatioSlot, kMidAboveRatioSlot,
            kMidRangeSlot, kMidLimitSlot, "Mid", -48.0f, -24.0f);
    setTiming(kMidAttackSlot, kMidReleaseSlot, "Mid");
    setBand(kHighLowerThresholdSlot, kHighUpperThresholdSlot, kHighBelowRatioSlot,
            kHighAboveRatioSlot, kHighRangeSlot, kHighLimitSlot, "High", -48.0f, -24.0f);
    setTiming(kHighAttackSlot, kHighReleaseSlot, "High");

    infos[kLowXoSlot] = {.name = "Low XO",
                         .unit = "Hz",
                         .scale = sdk::ParameterScale::Logarithmic,
                         .minValue = 40.0f,
                         .maxValue = 500.0f,
                         .defaultValue = 120.0f,
                         .scaleAnchor = 200.0f};
    infos[kHighXoSlot] = {.name = "High XO",
                          .unit = "Hz",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 500.0f,
                          .maxValue = 8000.0f,
                          .defaultValue = 2500.0f,
                          .scaleAnchor = 2000.0f};

    return infos;
}

void Multiband::Biquad::setLowPass(double sampleRate, double frequency) {
    frequency = std::clamp(frequency, 10.0, sampleRate * 0.45);
    const double omega = 2.0 * std::numbers::pi_v<double> * frequency / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double alpha = sinOmega / (2.0 * kBiquadQ);
    const double a0 = 1.0 + alpha;

    b0 = (1.0 - cosOmega) * 0.5 / a0;
    b1 = (1.0 - cosOmega) / a0;
    b2 = b0;
    a1 = (-2.0 * cosOmega) / a0;
    a2 = (1.0 - alpha) / a0;
}

void Multiband::Biquad::setHighPass(double sampleRate, double frequency) {
    frequency = std::clamp(frequency, 10.0, sampleRate * 0.45);
    const double omega = 2.0 * std::numbers::pi_v<double> * frequency / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double alpha = sinOmega / (2.0 * kBiquadQ);
    const double a0 = 1.0 + alpha;

    b0 = (1.0 + cosOmega) * 0.5 / a0;
    b1 = -(1.0 + cosOmega) / a0;
    b2 = b0;
    a1 = (-2.0 * cosOmega) / a0;
    a2 = (1.0 - alpha) / a0;
}

void Multiband::Biquad::reset() {
    z1 = 0.0;
    z2 = 0.0;
}

float Multiband::Biquad::process(float x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(std::isfinite(y) ? y : 0.0);
}

void Multiband::CrossoverState::setCoefficients(double sampleRate, double lowHz, double highHz) {
    lowHz = std::clamp(lowHz, 40.0, 500.0);
    highHz = std::clamp(highHz, std::max(500.0, lowHz + 10.0), 8000.0);

    lowLp1.setLowPass(sampleRate, lowHz);
    lowLp2.setLowPass(sampleRate, lowHz);
    splitHp1.setHighPass(sampleRate, lowHz);
    splitHp2.setHighPass(sampleRate, lowHz);
    midLp1.setLowPass(sampleRate, highHz);
    midLp2.setLowPass(sampleRate, highHz);
    highHp1.setHighPass(sampleRate, highHz);
    highHp2.setHighPass(sampleRate, highHz);
}

void Multiband::CrossoverState::reset() {
    lowLp1.reset();
    lowLp2.reset();
    splitHp1.reset();
    splitHp2.reset();
    midLp1.reset();
    midLp2.reset();
    highHp1.reset();
    highHp2.reset();
}

void Multiband::CrossoverState::split(float input, float& low, float& mid, float& high) {
    low = lowLp2.process(lowLp1.process(input));
    const float aboveLow = splitHp2.process(splitHp1.process(input));
    mid = midLp2.process(midLp1.process(aboveLow));
    high = highHp2.process(highHp1.process(aboveLow));
}

sdk::RestoreResult Multiband::restoreState(const sdk::StateNode& state) {
    curveCollapsed_ = state.getBool(kCurveCollapsedKey, true);
    return sdk::RestoreResult::success();
}

void Multiband::setCurveCollapsed(bool collapsed) {
    curveCollapsed_ = collapsed;

    // The toggle is the device's own state: the model's document has no other way to learn it.
    if (auto* reportTo = host()) {
        sdk::StateNode state;
        state.setBool(kCurveCollapsedKey, collapsed);
        reportTo->stateChanged(std::move(state));
    }
}

void Multiband::onPrepare(double, int) {
    updateCrossoverCoefficients(slotDisplayValue(kLowXoSlot), slotDisplayValue(kHighXoSlot));
    onReset();
}

void Multiband::onReset() {
    for (auto& crossover : crossovers_)
        crossover.reset();
    for (auto& channel : envelopes_)
        channel.fill(0.0f);
    for (auto& channel : gainDb_)
        channel.fill(0.0f);
}

void Multiband::updateCrossoverCoefficients(float lowXoHz, float highXoHz) {
    lowXoHz = std::clamp(lowXoHz, 40.0f, 500.0f);
    highXoHz = std::clamp(highXoHz, std::max(500.0f, lowXoHz + 10.0f), 8000.0f);
    for (auto& crossover : crossovers_)
        crossover.setCoefficients(currentSampleRate(), lowXoHz, highXoHz);
}

void Multiband::processAudio(sdk::ProcessContext& context) {
    // No Faust engine: the Linkwitz-Riley split and the three dynamics stages are the whole block.
    const int hostChannels = std::min(2, context.audio.numChannels());
    if (hostChannels <= 0)
        return;

    const float amount = std::clamp(slotDisplayValue(kAmountSlot), 0.0f, 1.0f);
    const float attackMs = slotDisplayValue(kAttackSlot);
    const float releaseMs = slotDisplayValue(kReleaseSlot);
    const float inputGain = dbToGain(slotDisplayValue(kInputSlot));
    const float outputGain = dbToGain(slotDisplayValue(kOutputSlot));
    const float mix = std::clamp(slotDisplayValue(kMixSlot), 0.0f, 1.0f);
    const float lowXoHz = slotDisplayValue(kLowXoSlot);
    const float highXoHz = slotDisplayValue(kHighXoSlot);

    updateCrossoverCoefficients(lowXoHz, highXoHz);

    const std::array<float, 3> lowerThresholds{slotDisplayValue(kLowLowerThresholdSlot),
                                               slotDisplayValue(kMidLowerThresholdSlot),
                                               slotDisplayValue(kHighLowerThresholdSlot)};
    const std::array<float, 3> upperThresholds{slotDisplayValue(kLowUpperThresholdSlot),
                                               slotDisplayValue(kMidUpperThresholdSlot),
                                               slotDisplayValue(kHighUpperThresholdSlot)};
    const std::array<float, 3> belowRatios{slotDisplayValue(kLowBelowRatioSlot),
                                           slotDisplayValue(kMidBelowRatioSlot),
                                           slotDisplayValue(kHighBelowRatioSlot)};
    const std::array<float, 3> aboveRatios{slotDisplayValue(kLowAboveRatioSlot),
                                           slotDisplayValue(kMidAboveRatioSlot),
                                           slotDisplayValue(kHighAboveRatioSlot)};
    const std::array<float, 3> ranges{slotDisplayValue(kLowRangeSlot),
                                      slotDisplayValue(kMidRangeSlot),
                                      slotDisplayValue(kHighRangeSlot)};
    const std::array<float, 3> limits{slotDisplayValue(kLowLimitSlot),
                                      slotDisplayValue(kMidLimitSlot),
                                      slotDisplayValue(kHighLimitSlot)};
    const float attackScale = std::clamp(attackMs / 3.0f, 0.1f, 10.0f);
    const float releaseScale = std::clamp(releaseMs / 120.0f, 0.1f, 10.0f);
    const std::array<float, 3> bandAttackMs{slotDisplayValue(kLowAttackSlot) * attackScale,
                                            slotDisplayValue(kMidAttackSlot) * attackScale,
                                            slotDisplayValue(kHighAttackSlot) * attackScale};
    const std::array<float, 3> bandReleaseMs{slotDisplayValue(kLowReleaseSlot) * releaseScale,
                                             slotDisplayValue(kMidReleaseSlot) * releaseScale,
                                             slotDisplayValue(kHighReleaseSlot) * releaseScale};
    const std::array<float, 3> bandInputGains{dbToGain(slotDisplayValue(kLowInputSlot)),
                                              dbToGain(slotDisplayValue(kMidInputSlot)),
                                              dbToGain(slotDisplayValue(kHighInputSlot))};
    const std::array<float, 3> bandGains{dbToGain(slotDisplayValue(kLowGainSlot)),
                                         dbToGain(slotDisplayValue(kMidGainSlot)),
                                         dbToGain(slotDisplayValue(kHighGainSlot))};

    const std::array<float, 3> attackCoeffs{coefficientForMs(bandAttackMs[0], currentSampleRate()),
                                            coefficientForMs(bandAttackMs[1], currentSampleRate()),
                                            coefficientForMs(bandAttackMs[2], currentSampleRate())};
    const std::array<float, 3> releaseCoeffs{
        coefficientForMs(bandReleaseMs[0], currentSampleRate()),
        coefficientForMs(bandReleaseMs[1], currentSampleRate()),
        coefficientForMs(bandReleaseMs[2], currentSampleRate())};
    const float gainSmoothCoeff = coefficientForMs(5.0f, currentSampleRate());
    const int numSamples = context.numSamples();

    for (int ch = 0; ch < hostChannels; ++ch) {
        float* buffer = context.audio.channel(ch);
        auto& crossover = crossovers_[static_cast<size_t>(ch)];
        auto& env = envelopes_[static_cast<size_t>(ch)];
        auto& smoothedGainDb = gainDb_[static_cast<size_t>(ch)];

        for (int i = 0; i < numSamples; ++i) {
            const float dry = buffer[i] * inputGain;
            float low = 0.0f, mid = 0.0f, high = 0.0f;
            crossover.split(dry, low, mid, high);
            std::array<float, 3> bands{low, mid, high};
            const float splitDry = low + mid + high;

            float wet = 0.0f;
            for (int band = 0; band < 3; ++band) {
                const auto idx = static_cast<size_t>(band);
                const float drivenBand = bands[idx] * bandInputGains[idx];
                const float detector = std::abs(drivenBand);
                const float envCoeff = detector > env[idx] ? attackCoeffs[idx] : releaseCoeffs[idx];
                env[idx] = envCoeff * env[idx] + (1.0f - envCoeff) * detector;

                const float levelDb = std::max(kMinLevelDb, gainToDb(env[idx]));
                const float targetGainDb =
                    dynamicsGainDb(levelDb, lowerThresholds[idx], upperThresholds[idx],
                                   belowRatios[idx], aboveRatios[idx], ranges[idx], amount);
                smoothedGainDb[idx] =
                    gainSmoothCoeff * smoothedGainDb[idx] + (1.0f - gainSmoothCoeff) * targetGainDb;

                float processed = drivenBand * dbToGain(smoothedGainDb[idx]) * bandGains[idx];
                const float ceiling = dbToGain(limits[idx]);
                processed = std::clamp(processed, -ceiling, ceiling);
                wet += processed;
            }

            const float out = ((1.0f - mix) * splitDry + mix * wet) * outputGain;
            buffer[i] = sanitise(out);
        }
    }

    for (int ch = hostChannels; ch < context.audio.numChannels(); ++ch)
        std::fill_n(context.audio.channel(ch), numSamples, 0.0f);
}

}  // namespace magda::devices::faust
