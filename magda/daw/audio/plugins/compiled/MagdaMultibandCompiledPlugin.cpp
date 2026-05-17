#include "plugins/compiled/MagdaMultibandCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterUtils.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaMultibandCompiledPlugin::xmlTypeName = "magda_multiband";

namespace {

constexpr float kMinRatio = 0.05f;
constexpr float kMaxRatio = 100.0f;
constexpr float kMinLevelDb = -100.0f;
constexpr double kBiquadQ = 0.7071067811865476;

magda::ParameterInfo parameterInfoForSlot(const CompiledHostSlotInfo& s) {
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.defaultValue = s.defaultValue;
    info.unit = s.unit;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return info;
}

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
    return 1.0f / juce::jlimit(kMinRatio, kMaxRatio, ratio);
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
    return juce::jlimit(-rangeDb, rangeDb, unclampedGainDb) * amount;
}

}  // namespace

void MagdaMultibandCompiledPlugin::Biquad::setLowPass(double sampleRate, double frequency) {
    frequency = juce::jlimit(10.0, sampleRate * 0.45, frequency);
    const double omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
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

void MagdaMultibandCompiledPlugin::Biquad::setHighPass(double sampleRate, double frequency) {
    frequency = juce::jlimit(10.0, sampleRate * 0.45, frequency);
    const double omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
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

void MagdaMultibandCompiledPlugin::Biquad::reset() {
    z1 = 0.0;
    z2 = 0.0;
}

float MagdaMultibandCompiledPlugin::Biquad::process(float x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(std::isfinite(y) ? y : 0.0);
}

void MagdaMultibandCompiledPlugin::CrossoverState::setCoefficients(double sampleRate, double lowHz,
                                                                   double highHz) {
    lowHz = juce::jlimit(40.0, 500.0, lowHz);
    highHz = juce::jlimit(std::max(500.0, lowHz + 10.0), 8000.0, highHz);

    lowLp1.setLowPass(sampleRate, lowHz);
    lowLp2.setLowPass(sampleRate, lowHz);
    splitHp1.setHighPass(sampleRate, lowHz);
    splitHp2.setHighPass(sampleRate, lowHz);
    midLp1.setLowPass(sampleRate, highHz);
    midLp2.setLowPass(sampleRate, highHz);
    highHp1.setHighPass(sampleRate, highHz);
    highHp2.setHighPass(sampleRate, highHz);
}

void MagdaMultibandCompiledPlugin::CrossoverState::reset() {
    lowLp1.reset();
    lowLp2.reset();
    splitHp1.reset();
    splitHp2.reset();
    midLp1.reset();
    midLp2.reset();
    highHp1.reset();
    highHp2.reset();
}

void MagdaMultibandCompiledPlugin::CrossoverState::split(float input, float& low, float& mid,
                                                         float& high) {
    low = lowLp2.process(lowLp1.process(input));
    const float aboveLow = splitHp2.process(splitHp1.process(input));
    mid = midLp2.process(midLp1.process(aboveLow));
    high = highHp2.process(highHp1.process(aboveLow));
}

MagdaMultibandCompiledPlugin::MagdaMultibandCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    buildHostParameters();
    updateCrossoverCoefficients(hostSlotInfo_[kLowXoSlot].defaultValue,
                                hostSlotInfo_[kHighXoSlot].defaultValue);
}

MagdaMultibandCompiledPlugin::~MagdaMultibandCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaMultibandCompiledPlugin::getName() const {
    return "Multiband Dynamics";
}
juce::String MagdaMultibandCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaMultibandCompiledPlugin::getShortName(int) {
    return "Dynamics";
}
juce::String MagdaMultibandCompiledPlugin::getSelectableDescription() {
    return "Multiband Dynamics";
}

void MagdaMultibandCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kAmountSlot] = {.name = "Amount",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = 0.0f,
                                  .maxValue = 1.0f,
                                  .defaultValue = 0.8f};
    hostSlotInfo_[kAttackSlot] = {.name = "Attack",
                                  .unit = "ms",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 0.1f,
                                  .maxValue = 100.0f,
                                  .defaultValue = 3.0f,
                                  .scaleAnchor = 10.0f};
    hostSlotInfo_[kReleaseSlot] = {.name = "Release",
                                   .unit = "ms",
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 5.0f,
                                   .maxValue = 1000.0f,
                                   .defaultValue = 120.0f,
                                   .scaleAnchor = 100.0f};
    hostSlotInfo_[kInputSlot] = {.name = "Input",
                                 .unit = "dB",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = -24.0f,
                                 .maxValue = 24.0f,
                                 .defaultValue = 0.0f};
    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = "dB",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 24.0f,
                                  .defaultValue = 0.0f};
    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 1.0f};
    auto setGain = [this](int slot, juce::String name) {
        hostSlotInfo_[slot] = {.name = std::move(name),
                               .unit = "dB",
                               .scale = magda::ParameterScale::Linear,
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

    auto setThreshold = [this](int slot, juce::String name, float defaultValue) {
        hostSlotInfo_[slot] = {.name = std::move(name),
                               .unit = "dB",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = -80.0f,
                               .maxValue = 0.0f,
                               .defaultValue = defaultValue};
    };
    auto setRatio = [this](int slot, juce::String name) {
        hostSlotInfo_[slot] = {.name = std::move(name),
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.05f,
                               .maxValue = kMaxRatio,
                               .defaultValue = 8.0f};
    };
    auto setTiming = [this](int attackSlot, int releaseSlot, const juce::String& bandName) {
        hostSlotInfo_[attackSlot] = {.name = bandName + " Attack",
                                     .unit = "ms",
                                     .scale = magda::ParameterScale::Logarithmic,
                                     .minValue = 0.1f,
                                     .maxValue = 100.0f,
                                     .defaultValue = 3.0f,
                                     .scaleAnchor = 10.0f};
        hostSlotInfo_[releaseSlot] = {.name = bandName + " Release",
                                      .unit = "ms",
                                      .scale = magda::ParameterScale::Logarithmic,
                                      .minValue = 5.0f,
                                      .maxValue = 1000.0f,
                                      .defaultValue = 120.0f,
                                      .scaleAnchor = 100.0f};
    };
    auto setBand = [this, setThreshold,
                    setRatio](int lowerThresholdSlot, int upperThresholdSlot, int belowRatioSlot,
                              int aboveRatioSlot, int rangeSlot, int limitSlot,
                              juce::String bandName, float lowerDefault, float upperDefault) {
        setThreshold(lowerThresholdSlot, bandName + " Lower Threshold", lowerDefault);
        setThreshold(upperThresholdSlot, bandName + " Upper Threshold", upperDefault);
        setRatio(belowRatioSlot, bandName + " Below Ratio");
        setRatio(aboveRatioSlot, bandName + " Above Ratio");
        hostSlotInfo_[rangeSlot] = {.name = bandName + " Range",
                                    .unit = "dB",
                                    .scale = magda::ParameterScale::Linear,
                                    .minValue = 0.0f,
                                    .maxValue = 48.0f,
                                    .defaultValue = 24.0f};
        hostSlotInfo_[limitSlot] = {.name = bandName + " Limit",
                                    .unit = "dB",
                                    .scale = magda::ParameterScale::Linear,
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

    hostSlotInfo_[kLowXoSlot] = {.name = "Low XO",
                                 .unit = "Hz",
                                 .scale = magda::ParameterScale::Logarithmic,
                                 .minValue = 40.0f,
                                 .maxValue = 500.0f,
                                 .defaultValue = 120.0f,
                                 .scaleAnchor = 200.0f};
    hostSlotInfo_[kHighXoSlot] = {.name = "High XO",
                                  .unit = "Hz",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 500.0f,
                                  .maxValue = 8000.0f,
                                  .defaultValue = 2500.0f,
                                  .scaleAnchor = 2000.0f};

    curveCollapsed_.referTo(state, juce::Identifier("magda_multiband_curve_collapsed"),
                            getUndoManager(), true);

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[i];
        const juce::String id = "magda_multiband_" + slot.name.toLowerCase().replace(" ", "_");
        const auto info = parameterInfoForSlot(slot);
        const float defaultNormalized =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[i].referTo(state, juce::Identifier(id), undoManager, defaultNormalized);

        auto param = addParam(
            id, slot.name, normalisedRange,
            [info](float normalized) {
                const float real = magda::ParameterUtils::normalizedToReal(normalized, info);
                return magda::ParameterUtils::formatValue(real, info);
            },
            [info](const juce::String& text) {
                auto parsed = magda::ParameterUtils::parseValue(text, info);
                if (parsed)
                    return magda::ParameterUtils::realToNormalized(*parsed, info);
                return 0.0f;
            });
        param->attachToCurrentValue(hostCached_[i]);
        hostParams_[i] = param;
    }
}

void MagdaMultibandCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
    updateCrossoverCoefficients(slotDisplayValue(kLowXoSlot), slotDisplayValue(kHighXoSlot));
    reset();
}

void MagdaMultibandCompiledPlugin::deinitialise() {}

void MagdaMultibandCompiledPlugin::reset() {
    for (auto& c : crossovers_)
        c.reset();
    for (auto& channel : envelopes_)
        channel.fill(0.0f);
    for (auto& channel : gainDb_)
        channel.fill(0.0f);
}

float MagdaMultibandCompiledPlugin::slotDisplayValue(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return 0.0f;
    if (auto* p = hostParams_[static_cast<size_t>(slotIndex)].get())
        return nativeValueToDisplayValue(slotIndex, p->getCurrentValue());
    return hostSlotInfo_[static_cast<size_t>(slotIndex)].defaultValue;
}

void MagdaMultibandCompiledPlugin::updateCrossoverCoefficients(float lowXoHz, float highXoHz) {
    lowXoHz = juce::jlimit(40.0f, 500.0f, lowXoHz);
    highXoHz = juce::jlimit(std::max(500.0f, lowXoHz + 10.0f), 8000.0f, highXoHz);
    for (auto& c : crossovers_)
        c.setCoefficients(sampleRate_, lowXoHz, highXoHz);
}

void MagdaMultibandCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    const int hostChannels = std::min(2, fc.destBuffer->getNumChannels());
    if (hostChannels <= 0)
        return;

    const float amount = juce::jlimit(0.0f, 1.0f, slotDisplayValue(kAmountSlot));
    const float attackMs = slotDisplayValue(kAttackSlot);
    const float releaseMs = slotDisplayValue(kReleaseSlot);
    const float inputGain = dbToGain(slotDisplayValue(kInputSlot));
    const float outputGain = dbToGain(slotDisplayValue(kOutputSlot));
    const float mix = juce::jlimit(0.0f, 1.0f, slotDisplayValue(kMixSlot));
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
    const float attackScale = juce::jlimit(0.1f, 10.0f, attackMs / 3.0f);
    const float releaseScale = juce::jlimit(0.1f, 10.0f, releaseMs / 120.0f);
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

    const std::array<float, 3> attackCoeffs{coefficientForMs(bandAttackMs[0], sampleRate_),
                                            coefficientForMs(bandAttackMs[1], sampleRate_),
                                            coefficientForMs(bandAttackMs[2], sampleRate_)};
    const std::array<float, 3> releaseCoeffs{coefficientForMs(bandReleaseMs[0], sampleRate_),
                                             coefficientForMs(bandReleaseMs[1], sampleRate_),
                                             coefficientForMs(bandReleaseMs[2], sampleRate_)};
    const float gainSmoothCoeff = coefficientForMs(5.0f, sampleRate_);
    const int startSample = fc.bufferStartSample;
    const int numSamples = fc.bufferNumSamples;

    for (int ch = 0; ch < hostChannels; ++ch) {
        float* buffer = fc.destBuffer->getWritePointer(ch, startSample);
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
                processed = juce::jlimit(-ceiling, ceiling, processed);
                wet += processed;
            }

            const float out = ((1.0f - mix) * splitDry + mix * wet) * outputGain;
            buffer[i] = std::isfinite(out) ? juce::jlimit(-16.0f, 16.0f, out) : 0.0f;
        }
    }

    for (int ch = hostChannels; ch < fc.destBuffer->getNumChannels(); ++ch)
        fc.destBuffer->clear(ch, startSample, numSamples);
}

te::AutomatableParameter* MagdaMultibandCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaMultibandCompiledPlugin::HostSlotInfo& MagdaMultibandCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaMultibandCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                              float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaMultibandCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                              float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"amount", 0, "Amount"},
    {"attack", 1, "Attack"},
    {"release", 2, "Release"},
    {"input", 3, "Input"},
    {"output", 4, "Output"},
    {"mix", 5, "Mix"},
    {"low_input", 6, "Low Input"},
    {"mid_input", 7, "Mid Input"},
    {"high_input", 8, "High Input"},
    {"low_gain", 9, "Low Output"},
    {"mid_gain", 10, "Mid Output"},
    {"high_gain", 11, "High Output"},
    {"low_lower_threshold", 12, "Low Lower Threshold"},
    {"low_upper_threshold", 13, "Low Upper Threshold"},
    {"low_below_ratio", 14, "Low Below Ratio"},
    {"low_above_ratio", 15, "Low Above Ratio"},
    {"low_range", 16, "Low Range"},
    {"low_limit", 17, "Low Limit"},
    {"low_attack", 18, "Low Attack"},
    {"low_release", 19, "Low Release"},
    {"mid_lower_threshold", 20, "Mid Lower Threshold"},
    {"mid_upper_threshold", 21, "Mid Upper Threshold"},
    {"mid_below_ratio", 22, "Mid Below Ratio"},
    {"mid_above_ratio", 23, "Mid Above Ratio"},
    {"mid_range", 24, "Mid Range"},
    {"mid_limit", 25, "Mid Limit"},
    {"mid_attack", 26, "Mid Attack"},
    {"mid_release", 27, "Mid Release"},
    {"high_lower_threshold", 28, "High Lower Threshold"},
    {"high_upper_threshold", 29, "High Upper Threshold"},
    {"high_below_ratio", 30, "High Below Ratio"},
    {"high_above_ratio", 31, "High Above Ratio"},
    {"high_range", 32, "High Range"},
    {"high_limit", 33, "High Limit"},
    {"high_attack", 34, "High Attack"},
    {"high_release", 35, "High Release"},
    {"low_xo", 36, "Low XO"},
    {"high_xo", 37, "High XO"},
};

const CompiledPluginSpec& getMagdaMultibandSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaMultibandCompiledPlugin::xmlTypeName,
        .displayName = "Multiband Dynamics",
        .browserCategory = "Dynamics",
        .description =
            "Native 3-band dynamics processor with independent lower and upper threshold "
            "regions per band. Ratios above 1:1 compress toward the active threshold; "
            "ratios below 1:1 expand away from it.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaMultibandCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
