#include "plugins/compiled/MagdaCompressorCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_compressor.generated.cpp"
#include "magda_compressor_glue.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaCompressorCompiledPlugin::xmlTypeName = "magda_compressor";

namespace {

float ampToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

/// The reduction the curve view draws at @p levelDb. A read of the same static
/// curve the dsp implements, not a tap of what it did: the dsp reports no gain
/// signal, and a meter that lagged the knobs by an envelope would read as the
/// device being wrong about its own settings.
float gainReductionForLevel(float levelDb, float thresholdDb, float ratio, float kneeDb) {
    ratio = std::max(1.0f, ratio);
    kneeDb = std::max(0.0f, kneeDb);

    const float over = levelDb - thresholdDb;
    float compressedOver = over;
    if (kneeDb > 0.0f) {
        const float halfKnee = kneeDb * 0.5f;
        if (over <= -halfKnee) {
            compressedOver = over;
        } else if (over >= halfKnee) {
            compressedOver = over / ratio;
        } else {
            const float x = over + halfKnee;
            compressedOver = over + (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
        }
    } else if (over > 0.0f) {
        compressedOver = over / ratio;
    }

    return std::max(0.0f, over - compressedOver);
}

/// Peak magnitude over one channel range of @p buffer.
float peakOfChannels(const juce::AudioBuffer<float>& buffer, int firstChannel, int lastChannel,
                     int startSample, int numSamples) {
    float peak = 0.0f;
    for (int channel = firstChannel; channel < std::min(lastChannel, buffer.getNumChannels());
         ++channel) {
        const float* samples = buffer.getReadPointer(channel, startSample);
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::fabs(samples[i]));
    }
    return peak;
}

}  // namespace

MagdaCompressorCompiledPlugin::MagdaCompressorCompiledPlugin() {
    initEffect();
}

::dsp* MagdaCompressorCompiledPlugin::createEngineDsp(int engineIndex) const {
    switch (static_cast<CompressorEngine>(engineIndex)) {
        case CompressorEngine::Clean:
            return new MagdaCompressorDsp();
        case CompressorEngine::Glue:
            return new MagdaCompressorGlueDsp();
    }
    return nullptr;
}

std::vector<MagdaCompressorCompiledPlugin::HostSlotInfo> MagdaCompressorCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = magda::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Clean", "Glue"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kThresholdSlot] = {.name = "Threshold",
                             .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                             .scale = magda::ParameterScale::Linear,
                             .minValue = -60.0f,
                             .maxValue = 0.0f,
                             .defaultValue = -18.0f};
    infos[kRatioSlot] = {.name = "Ratio",
                         .scale = magda::ParameterScale::Logarithmic,
                         .minValue = 1.0f,
                         .maxValue = 50.0f,
                         .defaultValue = 4.0f,
                         .scaleAnchor = 4.0f};
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                          .scale = magda::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 200.0f,
                          .defaultValue = 10.0f,
                          .scaleAnchor = 10.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                           .scale = magda::ParameterScale::Logarithmic,
                           .minValue = 5.0f,
                           .maxValue = 1000.0f,
                           .defaultValue = 120.0f,
                           .scaleAnchor = 100.0f};
    infos[kKneeSlot] = {.name = "Knee",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 0.0f,
                        .maxValue = 24.0f,
                        .defaultValue = 6.0f};
    infos[kMakeupSlot] = {.name = "Makeup",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 24.0f,
                          .defaultValue = 0.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    infos[kDetectorSlot].name = "Detector";
    infos[kDetectorSlot].scale = magda::ParameterScale::Discrete;
    infos[kDetectorSlot].choices = {"Peak", "RMS"};
    infos[kDetectorSlot].minValue = 0.0f;
    infos[kDetectorSlot].maxValue = 1.0f;
    infos[kDetectorSlot].defaultValue = 0.0f;

    infos[kLinkSlot] = {.name = "Link",
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 1.0f};
    infos[kSidechainHpfSlot] = {.name = "SC HPF",
                                .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                                .scale = magda::ParameterScale::Logarithmic,
                                .minValue = 20.0f,
                                .maxValue = 500.0f,
                                .defaultValue = 20.0f,
                                .scaleAnchor = 120.0f};
    infos[kFbffSlot] = {.name = "FBFF",
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.5f};
    infos[kStyleSlot].name = "Style";
    infos[kStyleSlot].scale = magda::ParameterScale::Discrete;
    infos[kStyleSlot].choices = {"Pre", "Post"};
    infos[kStyleSlot].minValue = 0.0f;
    infos[kStyleSlot].maxValue = 1.0f;
    infos[kStyleSlot].defaultValue = 0.0f;
    infos[kAutogainSlot].name = "Autogain";
    infos[kAutogainSlot].scale = magda::ParameterScale::Discrete;
    infos[kAutogainSlot].choices = {"Off", "On"};
    infos[kAutogainSlot].minValue = 0.0f;
    infos[kAutogainSlot].maxValue = 1.0f;
    infos[kAutogainSlot].defaultValue = 0.0f;

    return infos;
}

void MagdaCompressorCompiledPlugin::beforeCompute(DeviceProcessContext& context, int engineIndex) {
    const bool external = context.sidechainInputChannel >= 0;

    // The hidden zone that tells the dsp to detect off the key rather than off
    // its own input. Only the Clean engine has one; Glue has no external
    // sidechain and simply yields null here.
    if (auto* useSidechain = zoneForIdx(engineIndex, kUseSidechainHiddenSlot))
        *useSidechain = external ? 1.0f : 0.0f;

    const float inputPeak =
        peakOfChannels(*context.audio, 0, 2, context.startSample, context.numSamples);
    const float keyPeak = external ? peakOfChannels(*context.audio, context.sidechainInputChannel,
                                                    context.sidechainInputChannel + 1,
                                                    context.startSample, context.numSamples)
                                   : inputPeak;

    inputPeakDb_.store(ampToDb(inputPeak), std::memory_order_relaxed);
    keyPeakDb_.store(ampToDb(keyPeak), std::memory_order_relaxed);
    usingExternalSidechain_.store(external, std::memory_order_relaxed);

    const float keyDb = ampToDb(keyPeak);
    gainReductionDb_.store(gainReductionForLevel(keyDb, slotDisplayValue(kThresholdSlot),
                                                 slotDisplayValue(kRatioSlot),
                                                 slotDisplayValue(kKneeSlot)),
                           std::memory_order_relaxed);
}

void MagdaCompressorCompiledPlugin::afterCompute(DeviceProcessContext& context, int engineIndex) {
    juce::ignoreUnused(engineIndex);
    outputPeakDb_.store(
        ampToDb(peakOfChannels(*context.audio, 0, 2, context.startSample, context.numSamples)),
        std::memory_order_relaxed);
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},      {"threshold", 1, "Threshold"},
    {"ratio", 2, "Ratio"},        {"attack", 3, "Attack"},
    {"release", 4, "Release"},    {"knee", 5, "Knee"},
    {"makeup", 6, "Makeup"},      {"mix", 7, "Mix"},
    {"output", 8, "Output"},      {"detector", 9, "Detector"},
    {"link", 10, "Link"},         {"sc_hpf", 11, "SC HPF"},
    {"fbff", 12, "FBFF"},         {"style", 13, "Style"},
    {"autogain", 14, "Autogain"},
};

// Tracktion's retired Compressor loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"compressor"};

const CompiledPluginSpec& getMagdaCompressorSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaCompressorCompiledPlugin::xmlTypeName,
        .displayName = "Compressor",
        .browserCategory = "Dynamics",
        .description =
            "Compiled Faust compressor with selectable engines.\n"
            "<b>Clean</b>: feed-forward, peak/RMS detection, soft knee, stereo link, "
            "sidechain HPF, external audio sidechain, parallel mix, output safety limiting.\n"
            "<b>Glue</b>: Brouns FBFF compressor with exposed character controls "
            "(Detector Peak/RMS, Style Pre/Post, FBFF blend). No external sidechain.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaCompressorCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
