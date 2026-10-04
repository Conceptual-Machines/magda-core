#include "plugins/compiled/MagdaDimensionCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

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
