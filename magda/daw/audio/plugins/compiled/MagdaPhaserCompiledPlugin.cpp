#include "plugins/compiled/MagdaPhaserCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"rate", 0, "Rate"},     {"depth", 1, "Depth"},   {"feedback", 2, "Feedback"},
    {"stages", 3, "Stages"}, {"min_hz", 4, "Min Hz"}, {"max_hz", 5, "Max Hz"},
    {"mix", 6, "Mix"},
};

// Tracktion's retired Phaser loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"phaser"};

const CompiledPluginSpec& getMagdaPhaserSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPhaserCompiledPlugin::xmlTypeName,
        .displayName = "Phaser",
        .browserCategory = "Modulation",
        .description = "Compiled Faust stereo phaser with a sweeping notch comb. "
                       "Stages selects 2, 4, 6, or 8 notches; all four counts are instantiated "
                       "in parallel, so switching is glitch-free. "
                       "Rate and Depth drive the sweep; Feedback intensifies the resonance; "
                       "Min Hz and Max Hz bound the sweep window. "
                       "Mix blends wet against dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaPhaserCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
