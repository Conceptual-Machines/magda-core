#include "plugins/compiled/MagdaUtilityCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kUtilAliases[] = {
    {"gain", 0, "Gain"},    {"pan", 1, "Pan"},
    {"width", 2, "Width"},  {"lowmonofreq", 3, "Low Mono Freq"},
    {"mono", 4, "Mono"},    {"lowmono", 5, "Low Mono"},
    {"flipl", 6, "Flip L"}, {"flipr", 7, "Flip R"},
};

const CompiledPluginSpec& getMagdaUtilitySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaUtilityCompiledPlugin::xmlTypeName,
        .displayName = "Utility",
        .browserCategory = "Utility",
        .description =
            "Stereo utility stage. Gain trims level; Pan shifts the stereo image; "
            "Width adjusts the M/S spread. Mono folds the signal down for compatibility checks; "
            "Low Mono sums only the bass below the Low Mono Freq cutoff, "
            "tightening sub content while preserving stereo highs. "
            "Flip L / Flip R invert per-channel polarity for phase tweaks.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaUtilityCompiledPlugin>();
        },
        .aliasKey = "utility",
        .aliases = kUtilAliases,
        .aliasCount = static_cast<int>(sizeof(kUtilAliases) / sizeof(kUtilAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
