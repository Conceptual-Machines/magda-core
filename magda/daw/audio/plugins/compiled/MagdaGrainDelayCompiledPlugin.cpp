#include "plugins/compiled/MagdaGrainDelayCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_granular_delay.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaGrainDelayCompiledPlugin::xmlTypeName = "magda_grain_delay";

MagdaGrainDelayCompiledPlugin::MagdaGrainDelayCompiledPlugin() {
    initEffect();
}

::dsp* MagdaGrainDelayCompiledPlugin::createEngineDsp(int) const {
    return new MagdaGrainDelayDsp();
}

std::vector<MagdaGrainDelayCompiledPlugin::HostSlotInfo> MagdaGrainDelayCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own [style:menu{...}]: the labels
    // read as musical divisions and the values are the quarter-note
    // multipliers the zone wants, so neither list is restated here.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kTimeSlot] = {.name = "Time",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 1.0f,
                        .maxValue = 2000.0f,
                        .defaultValue = 500.0f};
    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = magda::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};
    infos[kSizeSlot] = {.name = "Size",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 20.0f,
                        .maxValue = 500.0f,
                        .defaultValue = 120.0f};
    infos[kPitchSlot] = {.name = "Pitch",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Semitones),
                         .scale = magda::ParameterScale::Linear,
                         .minValue = -24.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kSpraySlot] = {.name = "Spray",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = magda::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.30f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.40f};

    if (!divisionValues.empty()) {
        const int n = static_cast<int>(divisionValues.size());
        infos[kDivisionSlot].choices = menuLabelsForIdx(kDivisionSlot);
        infos[kDivisionSlot].maxValue = static_cast<float>(n - 1);
        for (int i = 0; i < n; ++i) {
            if (std::abs(divisionValues[static_cast<size_t>(i)] - 1.0f) < 1e-3f) {
                infos[kDivisionSlot].defaultValue = static_cast<float>(i);
                break;
            }
        }
    }

    return infos;
}

constexpr AliasSpec kAliases[] = {
    {"time", 0, "Time"},         {"division", 1, "Division"}, {"sync", 2, "Sync"},
    {"size", 3, "Size"},         {"pitch", 4, "Pitch"},       {"spray", 5, "Spray"},
    {"feedback", 6, "Feedback"}, {"mix", 7, "Mix"},
};

const CompiledPluginSpec& getMagdaGrainDelaySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGrainDelayCompiledPlugin::xmlTypeName,
        .displayName = "Grain Delay",
        .browserCategory = "Delay",
        .description =
            "Compiled Faust granular delay. A feedback delay line is read through a 4-voice "
            "Hann-windowed grain bank with 25% overlap. "
            "Pitch shifts via per-grain read-offset drift; Spray jitters the per-grain position. "
            "Time spans the base delay, locking to musical Division when Sync is on. "
            "Feedback recirculates through the grain bank; Mix blends wet against dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaGrainDelayCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
