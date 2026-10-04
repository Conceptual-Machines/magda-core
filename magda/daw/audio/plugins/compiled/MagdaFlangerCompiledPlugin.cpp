#include "plugins/compiled/MagdaFlangerCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"sync", 0, "Sync"},         {"rate", 1, "Rate"}, {"div", 2, "Division"}, {"depth", 3, "Depth"},
    {"feedback", 4, "Feedback"}, {"mix", 5, "Mix"},   {"width", 6, "Width"},
};

const CompiledPluginSpec& getMagdaFlangerSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFlangerCompiledPlugin::xmlTypeName,
        .displayName = "Flanger",
        .browserCategory = "Modulation",
        .description = "Compiled Faust stereo flanger. Short modulated delay per channel "
                       "(~3 ms +/- 2.5 ms) with a heavy feedback loop for the classic "
                       "comb-filter sweep. "
                       "Rate runs free in Hz or locks to tempo Division; "
                       "Depth, Feedback, Mix and Width round out the controls.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaFlangerCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
