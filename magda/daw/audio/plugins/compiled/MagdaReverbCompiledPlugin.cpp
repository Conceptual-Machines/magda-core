#include "plugins/compiled/MagdaReverbCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},     {"mix", 1, "Mix"},         {"predelay", 2, "Predelay"},
    {"decay", 3, "Decay"},       {"damping", 4, "Damping"}, {"low_cut", 5, "Low Cut"},
    {"high_cut", 6, "High Cut"}, {"width", 7, "Width"},     {"output", 8, "Output"},
};

// Tracktion's retired Reverb loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"reverb"};

const CompiledPluginSpec& getMagdaReverbSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaReverbCompiledPlugin::xmlTypeName,
        .displayName = "Reverb",
        .browserCategory = "Reverb",
        .description = "Compiled Faust reverb with three selectable engines.\n"
                       "<b>Plate</b>: Dattorro diffusion network for studio-plate ambience.\n"
                       "<b>Hall</b>: Zita 8-tap FDN for smooth large-space tails.\n"
                       "<b>Room</b>: Freeverb Schroeder/Moorer network for small-space ambience.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaReverbCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
