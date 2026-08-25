#include "plugins/compiled/MagdaUtilityCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaUtilityCompiledPlugin::xmlTypeName = "magda_utility";

namespace {

float onePoleAlpha(float cutoffHz, double sampleRate) {
    const float sr = static_cast<float>(std::max(1.0, sampleRate));
    const float cutoff = juce::jlimit(20.0f, sr * 0.45f, cutoffHz);
    return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * cutoff / sr);
}

}  // namespace

MagdaUtilityCompiledPlugin::MagdaUtilityCompiledPlugin() {
    initEffect();
}

std::vector<MagdaUtilityCompiledPlugin::HostSlotInfo> MagdaUtilityCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kGainSlot] = {.name = "Gain",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                        .scale = magda::ParameterScale::FaderDB,
                        .minValue = -60.0f,
                        .maxValue = 12.0f,
                        .defaultValue = 0.0f};
    infos[kPanSlot] = {.name = "Pan",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = -1.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.0f};
    infos[kWidthSlot] = {.name = "Width",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Percent),
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 200.0f,
                         .defaultValue = 100.0f};
    infos[kLowMonoFreqSlot] = {.name = "Low Mono Freq",
                               .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                               .scale = magda::ParameterScale::Logarithmic,
                               .minValue = 20.0f,
                               .maxValue = 500.0f,
                               .defaultValue = 120.0f,
                               .scaleAnchor = 120.0f};
    infos[kMonoSlot] = {.name = "Mono",
                        .scale = magda::ParameterScale::Boolean,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f};
    infos[kLowMonoSlot] = {.name = "Low Mono",
                           .scale = magda::ParameterScale::Boolean,
                           .minValue = 0.0f,
                           .maxValue = 1.0f,
                           .defaultValue = 0.0f};
    infos[kFlipLSlot] = {.name = "Flip L",
                         .scale = magda::ParameterScale::Boolean,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    infos[kFlipRSlot] = {.name = "Flip R",
                         .scale = magda::ParameterScale::Boolean,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};

    return infos;
}

void MagdaUtilityCompiledPlugin::onReset() {
    lowMonoLpL1_ = 0.0f;
    lowMonoLpL2_ = 0.0f;
    lowMonoLpR1_ = 0.0f;
    lowMonoLpR2_ = 0.0f;
}

void MagdaUtilityCompiledPlugin::processAudio(DeviceProcessContext& context) {
    // No Faust engine: gain, pan, M/S width and the Low Mono fold are a few
    // lines of arithmetic each, so this is the whole block.
    const int numSamples = context.numSamples;
    const int startSample = context.startSample;
    const int hostChannels = context.audio->getNumChannels();
    if (hostChannels <= 0)
        return;

    const float gainDb = slotDisplayValue(kGainSlot);
    const float gain = gainDb <= -59.99f ? 0.0f : juce::Decibels::decibelsToGain(gainDb);
    const float pan = juce::jlimit(-1.0f, 1.0f, slotDisplayValue(kPanSlot));
    const float width = juce::jlimit(0.0f, 200.0f, slotDisplayValue(kWidthSlot)) * 0.01f;
    const bool mono = slotDisplayValue(kMonoSlot) >= 0.5f;
    const bool lowMono = slotDisplayValue(kLowMonoSlot) >= 0.5f && !mono;
    const float flipL = slotDisplayValue(kFlipLSlot) >= 0.5f ? -1.0f : 1.0f;
    const float flipR = slotDisplayValue(kFlipRSlot) >= 0.5f ? -1.0f : 1.0f;
    const float panGainL = pan <= 0.0f ? 1.0f : 1.0f - pan;
    const float panGainR = pan >= 0.0f ? 1.0f : 1.0f + pan;
    const float lowMonoAlpha =
        onePoleAlpha(slotDisplayValue(kLowMonoFreqSlot), currentSampleRate());

    float* left = context.audio->getWritePointer(0, startSample);
    float* right = hostChannels > 1 ? context.audio->getWritePointer(1, startSample) : nullptr;

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

    // Anything past the stereo pair takes the gain trim and nothing else: the
    // rest of this device is a stereo image, and there is no image to shape.
    for (int channel = 2; channel < hostChannels; ++channel) {
        float* out = context.audio->getWritePointer(channel, startSample);
        for (int i = 0; i < numSamples; ++i)
            out[i] = sanitise(out[i] * gain);
    }
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
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaUtilityCompiledPlugin>();
        },
        .aliasKey = "utility",
        .aliases = kUtilAliases,
        .aliasCount = static_cast<int>(sizeof(kUtilAliases) / sizeof(kUtilAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
