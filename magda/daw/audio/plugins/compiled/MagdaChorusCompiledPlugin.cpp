#include "plugins/compiled/MagdaChorusCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_chorus.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaChorusCompiledPlugin::xmlTypeName = "magda_chorus";

MagdaChorusCompiledPlugin::MagdaChorusCompiledPlugin() {
    initEffect();
}

::dsp* MagdaChorusCompiledPlugin::createEngineDsp(int) const {
    return new MagdaChorusDsp();
}

std::vector<MagdaChorusCompiledPlugin::HostSlotInfo> MagdaChorusCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own [style:menu{...}]: the labels
    // read as musical divisions and the values are the quarter-note
    // multipliers the zone wants, so neither list is restated here.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kVoicesSlot].name = "Voices";
    infos[kVoicesSlot].scale = magda::ParameterScale::Discrete;
    infos[kVoicesSlot].choices = {"1", "2", "3"};
    infos[kVoicesSlot].minValue = 0.0f;
    infos[kVoicesSlot].maxValue = static_cast<float>(infos[kVoicesSlot].choices.size() - 1);
    infos[kVoicesSlot].defaultValue = 1.0f;  // 2 voices

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
                            .minValue = -0.9f,
                            .maxValue = 0.9f,
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
    {"voices", 0, "Voices"}, {"sync", 1, "Sync"},   {"rate", 2, "Rate"},
    {"div", 3, "Division"},  {"depth", 4, "Depth"}, {"feedback", 5, "Feedback"},
    {"mix", 6, "Mix"},       {"width", 7, "Width"},
};

// Tracktion's retired Chorus loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"chorus"};

const CompiledPluginSpec& getMagdaChorusSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaChorusCompiledPlugin::xmlTypeName,
        .displayName = "Chorus",
        .browserCategory = "Modulation",
        .description =
            "Compiled Faust stereo chorus with one to three modulated voices per channel. "
            "Voices share a single LFO with per-voice phase offsets for spread. "
            "Rate runs free in Hz or locks to tempo Division. "
            "Depth, Feedback, Mix and Width complete the controls.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaChorusCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
