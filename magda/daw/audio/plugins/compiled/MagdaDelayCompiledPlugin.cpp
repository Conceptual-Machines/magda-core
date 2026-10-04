#include "plugins/compiled/MagdaDelayCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"time", 0, "Time"},         {"division", 1, "Division"}, {"sync", 2, "Sync"},
    {"feedback", 3, "Feedback"}, {"mix", 4, "Mix"},           {"tone", 5, "Tone"},
    {"cross", 6, "Cross"},
};

// Tracktion's retired Delay loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"delay"};

const CompiledPluginSpec& getMagdaDelaySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaDelayCompiledPlugin::xmlTypeName,
        .displayName = "Delay",
        .browserCategory = "Delay",
        .description = "Compiled Faust stereo digital delay with fractional-sample interpolation. "
                       "Time spans 1 ms to 2 s; Sync locks to musical Division. "
                       "Feedback recirculates with Tone shaping the regen path. "
                       "Cross routes feedback across channels for ping-pong patterns.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaDelayCompiledPlugin>();
        },
        .aliasKey = "magda_delay_compiled",
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
