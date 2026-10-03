#include "plugins/compiled/MagdaModCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"mode", 0, "Mode"},    {"sync", 1, "Sync"},   {"rate", 2, "Rate"},
    {"div", 3, "Division"}, {"depth", 4, "Depth"}, {"shape", 5, "Shape"},
};

const CompiledPluginSpec& getMagdaModSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaModCompiledPlugin::xmlTypeName,
        .displayName = "Mod",
        .browserCategory = "Modulation",
        .description = "Compiled Faust modulation effect with a shared LFO.\n"
                       "<b>Tremolo</b>: amplitude modulation, equal on both channels.\n"
                       "<b>Vibrato</b>: pitch modulation via short modulated delay.\n"
                       "<b>Autopan</b>: equal-power pan between L and R.\n"
                       "All three mode bodies run in parallel for glitch-free switching. "
                       "LFO Shape selects Sine, Triangle, Square or Sample-and-hold; "
                       "Rate runs free in Hz or locks to tempo Division.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaModCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
