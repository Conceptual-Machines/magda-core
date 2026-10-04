#include "plugins/compiled/MagdaRingModCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"sync", 0, "Sync"}, {"freq", 1, "Frequency"}, {"div", 2, "Division"},  {"shape", 3, "Shape"},
    {"mix", 4, "Mix"},   {"width", 5, "Width"},    {"source", 6, "Source"},
};

const CompiledPluginSpec& getMagdaRingModSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaRingModCompiledPlugin::xmlTypeName,
        .displayName = "Ring Mod",
        .browserCategory = "Modulation",
        .description =
            "Compiled Faust stereo ring modulator. Multiplies the input by an internal carrier "
            "from 1 Hz (slow tremolo) to 5 kHz (metallic clang).\n"
            "<b>Sine</b>: pure tonal carrier, cleanest sideband structure.\n"
            "<b>Triangle</b>: softer overtone series than square.\n"
            "<b>Square</b>: rich odd-harmonic spectrum, aggressive sideband stack.\n"
            "Rate runs free in Hz or locks to tempo Division. "
            "Width offsets the carrier phase per channel; Mix blends wet against dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaRingModCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
