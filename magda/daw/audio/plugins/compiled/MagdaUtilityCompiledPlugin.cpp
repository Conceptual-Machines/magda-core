#include "plugins/compiled/MagdaUtilityCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterUtils.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaUtilityCompiledPlugin::xmlTypeName = "magda_utility";

namespace {

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

float onePoleAlpha(float cutoffHz, int sampleRate) {
    const float sr = static_cast<float>(std::max(1, sampleRate));
    const float cutoff = juce::jlimit(20.0f, sr * 0.45f, cutoffHz);
    return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoff / sr);
}

float sanitise(float sample) {
    return std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
}

}  // namespace

MagdaUtilityCompiledPlugin::MagdaUtilityCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    buildHostParameters();
}

MagdaUtilityCompiledPlugin::~MagdaUtilityCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaUtilityCompiledPlugin::getName() const {
    return "Utility";
}
juce::String MagdaUtilityCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaUtilityCompiledPlugin::getShortName(int) {
    return "Util";
}
juce::String MagdaUtilityCompiledPlugin::getSelectableDescription() {
    return "Utility";
}

void MagdaUtilityCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kGainSlot] = {.name = "Gain",
                                .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                                .scale = magda::ParameterScale::FaderDB,
                                .minValue = -60.0f,
                                .maxValue = 12.0f,
                                .defaultValue = 0.0f};
    hostSlotInfo_[kPanSlot] = {.name = "Pan",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = -1.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.0f};
    hostSlotInfo_[kWidthSlot] = {.name = "Width",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 2.0f,
                                 .defaultValue = 1.0f};
    hostSlotInfo_[kLowMonoFreqSlot] = {.name = "Low Mono Freq",
                                       .unit =
                                           magda::technicalText(magda::TechnicalTextToken::Hertz),
                                       .scale = magda::ParameterScale::Logarithmic,
                                       .minValue = 20.0f,
                                       .maxValue = 500.0f,
                                       .defaultValue = 120.0f,
                                       .scaleAnchor = 120.0f};
    hostSlotInfo_[kMonoSlot] = {.name = "Mono",
                                .scale = magda::ParameterScale::Boolean,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f};
    hostSlotInfo_[kLowMonoSlot] = {.name = "Low Mono",
                                   .scale = magda::ParameterScale::Boolean,
                                   .minValue = 0.0f,
                                   .maxValue = 1.0f,
                                   .defaultValue = 0.0f};
    hostSlotInfo_[kFlipLSlot] = {.name = "Flip L",
                                 .scale = magda::ParameterScale::Boolean,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};
    hostSlotInfo_[kFlipRSlot] = {.name = "Flip R",
                                 .scale = magda::ParameterScale::Boolean,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[static_cast<size_t>(i)];
        const juce::String id = "magda_utility_" + slot.name.toLowerCase().replace(" ", "_");
        const auto info = parameterInfoForSlot(slot);
        const float defaultNormalised =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[static_cast<size_t>(i)].referTo(state, juce::Identifier(id), undoManager,
                                                    defaultNormalised);

        auto param = addParam(
            id, slot.name, normalisedRange,
            [info](float normalised) {
                const float real = magda::ParameterUtils::normalizedToReal(normalised, info);
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

void MagdaUtilityCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    sampleRate_ = std::max(1, static_cast<int>(std::round(info.sampleRate)));
    reset();
}

void MagdaUtilityCompiledPlugin::deinitialise() {}

void MagdaUtilityCompiledPlugin::reset() {
    lowMonoLpL1_ = 0.0f;
    lowMonoLpL2_ = 0.0f;
    lowMonoLpR1_ = 0.0f;
    lowMonoLpR2_ = 0.0f;
}

float MagdaUtilityCompiledPlugin::slotDisplayValue(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount ||
        !hostParams_[static_cast<size_t>(slotIndex)])
        return 0.0f;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::normalizedToReal(
        hostParams_[static_cast<size_t>(slotIndex)]->getCurrentValue(), info);
}

void MagdaUtilityCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0)
        return;

    const float gainDb = slotDisplayValue(kGainSlot);
    const float gain = gainDb <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain(gainDb);
    const float pan = juce::jlimit(-1.0f, 1.0f, slotDisplayValue(kPanSlot));
    const float width = juce::jlimit(0.0f, 2.0f, slotDisplayValue(kWidthSlot));
    const float lowMonoFreq = slotDisplayValue(kLowMonoFreqSlot);
    const bool mono = slotDisplayValue(kMonoSlot) >= 0.5f;
    const bool lowMono = slotDisplayValue(kLowMonoSlot) >= 0.5f && !mono;
    const float flipL = slotDisplayValue(kFlipLSlot) >= 0.5f ? -1.0f : 1.0f;
    const float flipR = slotDisplayValue(kFlipRSlot) >= 0.5f ? -1.0f : 1.0f;
    const float panGainL = pan <= 0.0f ? 1.0f : 1.0f - pan;
    const float panGainR = pan >= 0.0f ? 1.0f : 1.0f + pan;
    const float lowMonoAlpha = onePoleAlpha(lowMonoFreq, sampleRate_);

    float* left = fc.destBuffer->getWritePointer(0, startSample);
    float* right = hostChannels > 1 ? fc.destBuffer->getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float l = left[i] * flipL * gain;
        float r = (right != nullptr ? right[i] : left[i]) * flipR * gain;

        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r);
        l = mid + side * width;
        r = mid - side * width;

        if (mono) {
            l = mid;
            r = mid;
        } else if (lowMono) {
            lowMonoLpL1_ += lowMonoAlpha * (l - lowMonoLpL1_);
            lowMonoLpL2_ += lowMonoAlpha * (lowMonoLpL1_ - lowMonoLpL2_);
            lowMonoLpR1_ += lowMonoAlpha * (r - lowMonoLpR1_);
            lowMonoLpR2_ += lowMonoAlpha * (lowMonoLpR1_ - lowMonoLpR2_);

            const float lowL = lowMonoLpL2_;
            const float lowR = lowMonoLpR2_;
            const float lowMid = 0.5f * (lowL + lowR);
            l = lowMid + (l - lowL);
            r = lowMid + (r - lowR);
        }

        l *= panGainL;
        r *= panGainR;

        left[i] = sanitise(l);
        if (right != nullptr)
            right[i] = sanitise(r);
    }

    for (int ch = 2; ch < hostChannels; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            out[i] = sanitise(out[i] * gain);
    }
}

te::AutomatableParameter* MagdaUtilityCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaUtilityCompiledPlugin::HostSlotInfo& MagdaUtilityCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaUtilityCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                            float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    return magda::ParameterUtils::realToNormalized(
        displayValue, parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]));
}

float MagdaUtilityCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                            float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    return magda::ParameterUtils::normalizedToReal(
        nativeValue, parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]));
}

constexpr AliasSpec kUtilAliases[] = {
    {"gain", 0, "Gain"},    {"pan", 1, "Pan"},
    {"width", 2, "Width"},  {"lowmonofreq", 3, "Low Mono Freq"},
    {"mono", 4, "Mono"},    {"lowmono", 5, "Low Mono"},
    {"flipl", 6, "Flip L"}, {"flipr", 7, "Flip R"},
};

const CompiledPluginSpec& getMagdaUtilitySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaUtilityCompiledPlugin::xmlTypeName,
        .displayName = "Utility",
        .browserCategory = "Utility",
        .description =
            "Stereo utility stage. Gain trims level; Pan shifts the stereo image; "
            "Width adjusts the M/S spread. Mono folds the signal down for compatibility checks; "
            "Low Mono sums only the bass below the Low Mono Freq cutoff, "
            "tightening sub content while preserving stereo highs. "
            "Flip L / Flip R invert per-channel polarity for phase tweaks.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaUtilityCompiledPlugin(info);
        },
        .aliasKey = "utility",
        .aliases = kUtilAliases,
        .aliasCount = static_cast<int>(sizeof(kUtilAliases) / sizeof(kUtilAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
