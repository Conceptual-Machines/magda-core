#include "plugins/compiled/MagdaBitcrusherCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

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
