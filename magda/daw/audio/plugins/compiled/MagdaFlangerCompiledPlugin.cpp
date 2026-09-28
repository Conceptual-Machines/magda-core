#include "plugins/compiled/MagdaFlangerCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_flanger.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaFlangerCompiledPlugin::xmlTypeName = "magda_flanger";

MagdaFlangerCompiledPlugin::MagdaFlangerCompiledPlugin() {
    initEffect();
}

::dsp* MagdaFlangerCompiledPlugin::createEngineDsp(int) const {
    return new MagdaFlangerDsp();
}

std::vector<MagdaFlangerCompiledPlugin::HostSlotInfo> MagdaFlangerCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own [style:menu{...}]: the labels
    // read as musical divisions and the values are the quarter-note
    // multipliers the zone wants, so neither list is restated here.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = magda::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};

    infos[kRateSlot] = {.name = "Rate",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 10.0f,
                        .defaultValue = 0.5f,
                        .scaleAnchor = 0.5f};

    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;

    infos[kDepthSlot] = {.name = "Depth",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};

    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = magda::ParameterScale::Linear,
                            .minValue = -0.95f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.0f};

    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.5f};

    infos[kWidthSlot] = {.name = "Width",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};

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
    {"sync", 0, "Sync"},         {"rate", 1, "Rate"}, {"div", 2, "Division"}, {"depth", 3, "Depth"},
    {"feedback", 4, "Feedback"}, {"mix", 5, "Mix"},   {"width", 6, "Width"},
};

const CompiledPluginSpec& getMagdaFlangerSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFlangerCompiledPlugin::xmlTypeName,
        .displayName = "Flanger",
        .browserCategory = "Modulation",
        .description = "Compiled Faust stereo flanger. Short modulated delay per channel "
                       "(~3 ms +/- 2.5 ms) with a heavy feedback loop for the classic "
                       "comb-filter sweep. "
                       "Rate runs free in Hz or locks to tempo Division; "
                       "Depth, Feedback, Mix and Width round out the controls.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaFlangerCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
