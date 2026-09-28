#include "plugins/compiled/MagdaBitcrusherCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_bitcrusher.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaBitcrusherCompiledPlugin::xmlTypeName = "magda_bitcrusher";

MagdaBitcrusherCompiledPlugin::MagdaBitcrusherCompiledPlugin() {
    initEffect();
}

::dsp* MagdaBitcrusherCompiledPlugin::createEngineDsp(int) const {
    return new MagdaBitcrusherDsp();
}

std::vector<MagdaBitcrusherCompiledPlugin::HostSlotInfo> MagdaBitcrusherCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kRateSlot] = {.name = "Rate",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 100.0f,
                        .maxValue = 48000.0f,
                        .defaultValue = 8000.0f,
                        .scaleAnchor = 4000.0f};
    infos[kBitsSlot] = {.name = "Bits",
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 1.0f,
                        .maxValue = 16.0f,
                        .defaultValue = 8.0f};
    infos[kDriveSlot] = {.name = "Drive",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kToneSlot] = {.name = "Tone",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 200.0f,
                        .maxValue = 20000.0f,
                        .defaultValue = 20000.0f,
                        .scaleAnchor = 2000.0f};
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
    {"rate", 0, "Rate"}, {"bits", 1, "Bits"}, {"drive", 2, "Drive"},
    {"tone", 3, "Tone"}, {"mix", 4, "Mix"},   {"output", 5, "Output"},
};

const CompiledPluginSpec& getMagdaBitcrusherSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaBitcrusherCompiledPlugin::xmlTypeName,
        .displayName = "Bitcrusher",
        .browserCategory = "Distortion",
        .description =
            "Compiled Faust lo-fi bitcrusher. "
            "Rate reduces sample rate via dual sample-and-hold (100 Hz to 48 kHz). "
            "Bits applies mid-tread quantization from 1 to 16 bits. "
            "Drive shifts the quantization landing point for crunchier or softer attacks. "
            "Tone tames aliasing with a post-crush low-pass. "
            "Mix and Output blend and trim.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaBitcrusherCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
