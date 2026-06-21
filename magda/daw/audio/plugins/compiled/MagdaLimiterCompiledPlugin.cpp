#include "plugins/compiled/MagdaLimiterCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterUtils.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaLimiterCompiledPlugin::xmlTypeName = "magda_limiter";

namespace {

float ampToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

float realForSlot(const MagdaLimiterCompiledPlugin::HostSlotInfo& s,
                  te::AutomatableParameter* param) {
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::normalizedToReal(
        param != nullptr ? param->getCurrentValue() : 0.0f, info);
}

}  // namespace

float MagdaLimiterDspCore::dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float MagdaLimiterDspCore::coefficient(float timeMs, double sampleRate) {
    const auto samples = std::max(1.0, static_cast<double>(timeMs) * 0.001 * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

void MagdaLimiterDspCore::prepare(double sampleRate, int, int numChannels) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    delaySamples_ = std::max(1, static_cast<int>(std::ceil(sampleRate_ * 0.005)));
    const auto channels = static_cast<size_t>(std::max(1, numChannels));
    const auto lineLength = static_cast<size_t>(delaySamples_ + 1);

    delayLines_.assign(channels, std::vector<float>(lineLength, 0.0f));
    frame_.assign(channels, 0.0f);
    reset();
}

void MagdaLimiterDspCore::reset() {
    writeIndex_ = 0;
    gain_ = 1.0f;
    for (auto& line : delayLines_)
        std::fill(line.begin(), line.end(), 0.0f);
}

MagdaLimiterDspCore::Stats MagdaLimiterDspCore::process(juce::AudioBuffer<float>& buffer,
                                                        int startSample, int numSamples,
                                                        const Settings& settings) {
    Stats stats;
    const int channels = buffer.getNumChannels();
    if (channels <= 0 || numSamples <= 0)
        return stats;

    if (static_cast<int>(delayLines_.size()) < channels)
        prepare(sampleRate_, numSamples, channels);

    const float thresholdDb = juce::jlimit(-24.0f, 0.0f, settings.thresholdDb);
    const float preGain = dbToGain(-thresholdDb);
    const float outputGain = dbToGain(juce::jlimit(-24.0f, 0.0f, settings.outputDb));
    const float attackCoeff = coefficient(std::max(0.1f, settings.attackMs), sampleRate_);
    const float releaseCoeff = coefficient(std::max(10.0f, settings.releaseMs), sampleRate_);
    const int lineLength = static_cast<int>(delayLines_.front().size());

    float maxReduction = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float detectorPeak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float input = buffer.getSample(ch, startSample + i);
            const float finiteInput = std::isfinite(input) ? input : 0.0f;
            stats.inputPeak = std::max(stats.inputPeak, std::abs(finiteInput));

            const float driven = finiteInput * preGain;
            delayLines_[static_cast<size_t>(ch)][static_cast<size_t>(writeIndex_)] = driven;
            detectorPeak = std::max(detectorPeak, std::abs(driven));
        }

        const float desiredGain = detectorPeak > 1.0f ? 1.0f / detectorPeak : 1.0f;
        const float coeff = desiredGain < gain_ ? attackCoeff : releaseCoeff;
        gain_ = desiredGain + coeff * (gain_ - desiredGain);
        maxReduction = std::max(maxReduction, gain_ < 1.0f ? -ampToDb(gain_) : 0.0f);

        const int readIndex = (writeIndex_ + 1) % lineLength;
        float postPeak = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float limited =
                delayLines_[static_cast<size_t>(ch)][static_cast<size_t>(readIndex)] * gain_;
            frame_[static_cast<size_t>(ch)] = limited;
            postPeak = std::max(postPeak, std::abs(limited));
        }

        const float safetyGain = postPeak > 1.0f ? 1.0f / postPeak : 1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const float output = frame_[static_cast<size_t>(ch)] * safetyGain * outputGain;
            const float clean = std::isfinite(output) ? juce::jlimit(-1.0f, 1.0f, output) : 0.0f;
            buffer.setSample(ch, startSample + i, clean);
            stats.outputPeak = std::max(stats.outputPeak, std::abs(clean));
        }

        writeIndex_ = (writeIndex_ + 1) % lineLength;
    }

    stats.gainReductionDb = maxReduction;
    return stats;
}

MagdaLimiterCompiledPlugin::MagdaLimiterCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    limiter_.prepare(44100.0, 512, 2);
    buildHostParameters();
}

MagdaLimiterCompiledPlugin::~MagdaLimiterCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaLimiterCompiledPlugin::getName() const {
    return "Limiter";
}
juce::String MagdaLimiterCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaLimiterCompiledPlugin::getShortName(int) {
    return "Limiter";
}
juce::String MagdaLimiterCompiledPlugin::getSelectableDescription() {
    return "Limiter";
}

void MagdaLimiterCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kThresholdSlot] = {.name = "Threshold",
                                     .unit =
                                         magda::technicalText(magda::TechnicalTextToken::Decibels),
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = -24.0f,
                                     .maxValue = 0.0f,
                                     .defaultValue = -1.0f};
    hostSlotInfo_[kAttackSlot] = {.name = "Attack",
                                  .unit =
                                      magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 0.1f,
                                  .maxValue = 50.0f,
                                  .defaultValue = 1.0f,
                                  .scaleAnchor = 1.0f};
    hostSlotInfo_[kReleaseSlot] = {
        .name = "Release",
        .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
        .scale = magda::ParameterScale::Logarithmic,
        .minValue = 10.0f,
        .maxValue = 2000.0f,
        .defaultValue = 200.0f,
        .scaleAnchor = 200.0f};
    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 0.0f,
                                  .defaultValue = 0.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    auto buildInfo = [](const HostSlotInfo& s) {
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
    };

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[static_cast<size_t>(i)];
        const juce::String id = "magda_limiter_" + slot.name.toLowerCase().replace(" ", "_");
        const juce::Identifier identifier(id);
        const auto info = buildInfo(slot);
        const float defaultNormalized =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[static_cast<size_t>(i)].referTo(state, identifier, undoManager,
                                                    defaultNormalized);

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
        param->attachToCurrentValue(hostCached_[static_cast<size_t>(i)]);
        hostParams_[static_cast<size_t>(i)] = param;
    }
}

void MagdaLimiterCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    limiter_.prepare(info.sampleRate, info.blockSizeSamples, 2);
}

void MagdaLimiterCompiledPlugin::deinitialise() {
    limiter_.reset();
}

void MagdaLimiterCompiledPlugin::reset() {
    limiter_.reset();
}

void MagdaLimiterCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    MagdaLimiterDspCore::Settings settings;
    settings.thresholdDb = realForSlot(hostSlotInfo_[kThresholdSlot], hostParams_[kThresholdSlot]);
    settings.attackMs = realForSlot(hostSlotInfo_[kAttackSlot], hostParams_[kAttackSlot]);
    settings.releaseMs = realForSlot(hostSlotInfo_[kReleaseSlot], hostParams_[kReleaseSlot]);
    settings.outputDb = realForSlot(hostSlotInfo_[kOutputSlot], hostParams_[kOutputSlot]);

    auto stats =
        limiter_.process(*fc.destBuffer, fc.bufferStartSample, fc.bufferNumSamples, settings);
    inputPeakDb_.store(ampToDb(stats.inputPeak), std::memory_order_relaxed);
    outputPeakDb_.store(ampToDb(stats.outputPeak), std::memory_order_relaxed);
    gainReductionDb_.store(stats.gainReductionDb, std::memory_order_relaxed);
}

te::AutomatableParameter* MagdaLimiterCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaLimiterCompiledPlugin::HostSlotInfo& MagdaLimiterCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaLimiterCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                            float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto& s = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaLimiterCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                            float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto& s = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"threshold", 0, "Threshold"},
    {"attack", 1, "Attack"},
    {"release", 2, "Release"},
    {"output", 3, "Output"},
};

const CompiledPluginSpec& getMagdaLimiterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaLimiterCompiledPlugin::xmlTypeName,
        .displayName = "Limiter",
        .browserCategory = "Dynamics",
        .description = "Native stereo lookahead limiter / autonormalizer. "
                       "Threshold drives the signal into a fixed 0 dB ceiling, "
                       "Attack and Release shape gain recovery, and Output is a "
                       "post-limiter trim limited to negative gain.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaLimiterCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
