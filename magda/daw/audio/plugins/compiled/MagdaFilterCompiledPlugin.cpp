#include "plugins/compiled/MagdaFilterCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"cutoff", 0, "Cutoff"}, {"resonance", 1, "Resonance"}, {"drive", 2, "Drive"},
    {"engine", 3, "Engine"}, {"mode", 4, "Mode"},           {"limit", 5, "Limit"},
};

// Tracktion's retired Lowpass loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"lowpass"};

const CompiledPluginSpec& getMagdaFilterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFilterCompiledPlugin::xmlTypeName,
        .displayName = "Filter",
        .browserCategory = "Filter",
        .description =
            "Compiled Faust multimode filter.\n"
            "<b>SVF</b>: clean 2-pole LP/BP/HP/Notch for precise shaping.\n"
            "<b>Ladder</b>: classic 4-pole low-pass with driven resonance.\n"
            "<b>Korg 35</b>: MS-style LP/HP character with sharper analog bite.\n"
            "<b>Oberheim</b>: SEM-style LP/BP/HP/Notch with broad musical sweeps.\n"
            "<b>Sallen-Key</b>: smooth 2nd-order LP/BP/HP response.\n"
            "<b>Diode</b>: resonant 4-pole diode ladder with input drive.\n"
            "<warning>Warning: high resonance can create very loud peaks or "
            "self-oscillation. "
            "Keep monitoring levels conservative to protect speakers and ears.</warning>",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaFilterCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
