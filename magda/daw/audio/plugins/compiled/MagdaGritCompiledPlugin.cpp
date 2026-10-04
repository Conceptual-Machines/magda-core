#include "plugins/compiled/MagdaGritCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"frequency", 0, "Frequency"},
    {"width", 1, "Width"},
    {"amount", 2, "Amount"},
    {"mode", 3, "Mode"},
};

const CompiledPluginSpec& getMagdaGritSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGritCompiledPlugin::xmlTypeName,
        .displayName = "Grit",
        .browserCategory = "Distortion",
        .description =
            "Compiled Faust texture generator. Ring-modulates the input with a tone "
            "or filtered-noise carrier for Erosion-style grit.\n"
            "<b>Noise</b>: shared mono bandpass-filtered noise on both channels.\n"
            "<b>Wide Noise</b>: decorrelated stereo noise for spatial texture.\n"
            "<b>Sine</b>: tonal sine carrier at the Frequency knob for metallic ring-mod.\n"
            "Frequency is the carrier centre (or BPF centre in the noise modes); "
            "Width sets the bandpass Q in Noise and Wide Noise modes; it has no effect "
            "in Sine mode. Amount blends the wet against the dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaGritCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
