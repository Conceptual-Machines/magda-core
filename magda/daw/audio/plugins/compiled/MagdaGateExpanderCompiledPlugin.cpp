#include "plugins/compiled/MagdaGateExpanderCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_gate_expander.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaGateExpanderCompiledPlugin::xmlTypeName = "magda_gate_expander";

MagdaGateExpanderCompiledPlugin::MagdaGateExpanderCompiledPlugin() {
    initEffect();
}

::dsp* MagdaGateExpanderCompiledPlugin::createEngineDsp(int) const {
    return new MagdaGateExpanderDsp();
}

std::vector<MagdaGateExpanderCompiledPlugin::HostSlotInfo>
MagdaGateExpanderCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                          .scale = magda::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 100.0f,
                          .defaultValue = 1.0f,
                          .scaleAnchor = 1.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                           .scale = magda::ParameterScale::Logarithmic,
                           .minValue = 5.0f,
                           .maxValue = 1000.0f,
                           .defaultValue = 120.0f,
                           .scaleAnchor = 100.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 24.0f,
                          .defaultValue = 0.0f};
    infos[kThresholdSlot] = {.name = "Threshold",
                             .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                             .scale = magda::ParameterScale::Linear,
                             .minValue = -80.0f,
                             .maxValue = 0.0f,
                             .defaultValue = -40.0f};
    infos[kRatioSlot] = {.name = "Ratio",
                         .scale = magda::ParameterScale::Logarithmic,
                         .minValue = 1.0f,
                         .maxValue = 50.0f,
                         .defaultValue = 4.0f,
                         .scaleAnchor = 4.0f};
    infos[kRangeSlot] = {.name = "Range",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 80.0f,
                         .defaultValue = 60.0f};

    return infos;
}

constexpr AliasSpec kAliases[] = {
    {"attack", 0, "Attack"}, {"release", 1, "Release"},     {"mix", 2, "Mix"},
    {"output", 3, "Output"}, {"threshold", 4, "Threshold"}, {"ratio", 5, "Ratio"},
    {"range", 6, "Range"},
};

const CompiledPluginSpec& getMagdaGateExpanderSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGateExpanderCompiledPlugin::xmlTypeName,
        .displayName = "Gate",
        .browserCategory = "Dynamics",
        .description =
            "Compiled Faust stereo gate / downward expander with a linked peak detector. "
            "Threshold sets where the gate opens; Ratio shapes the slope; "
            "Range bounds the deepest cut. "
            "Attack and Release shape the envelope; "
            "Mix blends the gated signal back against dry for parallel gating.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaGateExpanderCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
