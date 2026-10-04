#include "plugins/compiled/MagdaFreqShiftCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"shift", 0, "Shift"},
    {"feedback", 1, "Feedback"},
    {"mix", 2, "Mix"},
    {"spread", 3, "Spread"},
};

const CompiledPluginSpec& getMagdaFreqShiftSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFreqShiftCompiledPlugin::xmlTypeName,
        .displayName = "Freq Shift",
        .browserCategory = "Modulation",
        .description = "Compiled Faust stereo single-sideband frequency shifter. "
                       "Shifts the entire spectrum by a fixed Hz offset using a Bode design: "
                       "a Niemitalo Hilbert transformer (~80 dB image rejection) is "
                       "complex-multiplied with a phasor at the Shift frequency. "
                       "Unlike a pitch shifter the harmonic ratios are not preserved, "
                       "producing inharmonic, metallic timbres. "
                       "Feedback recirculates for resonant artefacts; "
                       "Spread detunes the channels by up to 25 Hz for chorus-style stereo.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaFreqShiftCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
