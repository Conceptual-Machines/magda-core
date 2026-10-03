#include "plugins/compiled/MagdaPitchCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},   {"pitch", 1, "Pitch"}, {"fine", 2, "Fine"},
    {"texture", 3, "Texture"}, {"mix", 4, "Mix"},     {"output", 5, "Output"},
};

// Tracktion's retired Pitch Shift loads here; see core/LegacyDeviceAliases.hpp.
// "pitch shift" is the retired device's display name, kept so an instruction
// that asks for one by that name still lands on this device.
constexpr const char* kLoadAliases[] = {"pitchShifter", "pitchshift", "pitch shift"};

const CompiledPluginSpec& getMagdaPitchSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPitchCompiledPlugin::xmlTypeName,
        .displayName = "Pitch",
        .browserCategory = "Pitch",
        .description = "Compiled Faust pitch shifter with three selectable engines.\n"
                       "<b>Shifter</b>: single voice, full plus/minus 24 semitones.\n"
                       "<b>Detuner</b>: two voices hard-panned L/R for chorus-style thickening.\n"
                       "<b>Harmonizer</b>: shifted voice summed with dry at a chosen interval.\n"
                       "All three use ef.transpose; transient smear and grain are by design.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaPitchCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
