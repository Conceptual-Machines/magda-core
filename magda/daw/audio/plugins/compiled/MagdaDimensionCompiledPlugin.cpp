#include "plugins/compiled/MagdaDimensionCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_dimension_dim.generated.cpp"
#include "magda_dimension_haas.generated.cpp"
#include "magda_dimension_ms.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaDimensionCompiledPlugin::xmlTypeName = "magda_dimension";

MagdaDimensionCompiledPlugin::MagdaDimensionCompiledPlugin() {
    initEffect();
}

::dsp* MagdaDimensionCompiledPlugin::createEngineDsp(int engineIndex) const {
    switch (static_cast<DimensionEngine>(engineIndex)) {
        case DimensionEngine::Dimension:
            return new MagdaDimensionDimDsp();
        case DimensionEngine::Haas:
            return new MagdaDimensionHaasDsp();
        case DimensionEngine::MidSide:
            return new MagdaDimensionMSDsp();
    }
    return nullptr;
}

std::vector<MagdaDimensionCompiledPlugin::HostSlotInfo> MagdaDimensionCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = magda::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Dimension", "Haas", "M/S"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kAmountSlot] = {.name = "Amount",
                          .scale = magda::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 0.5f};
    infos[kRateSlot] = {.name = "Rate",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 4.0f,
                        .defaultValue = 0.5f,
                        .scaleAnchor = 0.5f};
    infos[kWidthSlot] = {.name = "Width",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 200.0f,
                         .defaultValue = 100.0f};
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

    return infos;
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"}, {"amount", 1, "Amount"}, {"rate", 2, "Rate"},
    {"width", 3, "Width"},   {"mix", 4, "Mix"},       {"output", 5, "Output"},
};

const CompiledPluginSpec& getMagdaDimensionSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaDimensionCompiledPlugin::xmlTypeName,
        .displayName = "Dimension",
        .browserCategory = "Stereo",
        .description =
            "Compiled Faust stereo widener with three selectable engines.\n"
            "<b>Dimension</b>: Roland Dimension D-style anti-phase modulated delays.\n"
            "<b>Haas</b>: short fixed delay on one channel, classic psychoacoustic cue.\n"
            "<b>M/S</b>: pure mid-side side-channel gain, no time smear.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaDimensionCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
