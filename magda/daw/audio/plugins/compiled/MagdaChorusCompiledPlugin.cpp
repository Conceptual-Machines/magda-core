#include "plugins/compiled/MagdaChorusCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

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
