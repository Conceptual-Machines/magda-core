#include "plugins/compiled/MagdaLimiterCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"threshold", 0, "Threshold"},
    {"attack", 1, "Attack"},
    {"release", 2, "Release"},
    {"output", 3, "Output"},
};

const CompiledPluginSpec& getMagdaLimiterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaLimiterCompiledPlugin::xmlTypeName,
        .displayName = "Limiter",
        .browserCategory = "Dynamics",
        .description = "Native stereo lookahead limiter / autonormalizer. "
                       "Threshold drives the signal into a fixed 0 dB ceiling, "
                       "Attack and Release shape gain recovery, and Output is a "
                       "post-limiter trim limited to negative gain.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaLimiterCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
