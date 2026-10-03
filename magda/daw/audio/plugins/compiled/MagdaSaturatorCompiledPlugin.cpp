#include "plugins/compiled/MagdaSaturatorCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"drive", 0, "Drive"}, {"mode", 1, "Mode"}, {"bias", 2, "Bias"},
    {"tone", 3, "Tone"},   {"mix", 4, "Mix"},   {"output", 5, "Output"},
};

const CompiledPluginSpec& getMagdaSaturatorSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaSaturatorCompiledPlugin::xmlTypeName,
        .displayName = "Saturator",
        .browserCategory = "Distortion",
        .description =
            "Compiled Faust waveshaper with six selectable curves.\n"
            "<b>Tanh</b>: smooth hyperbolic, the classic warm saturation.\n"
            "<b>Soft</b>: gentle polynomial knee with a rolled-off top.\n"
            "<b>Hard</b>: instant clip ceiling for square-edged distortion.\n"
            "<b>Fold</b>: wavefolder, peaks reflect back for metallic overtones.\n"
            "<b>Tube</b>: asymmetric curve (1.4x positive, 1.0x negative) "
            "for valve-style even harmonics.\n"
            "<b>Tape</b>: tanh with an odd-order compression term, tape-style headroom.\n"
            "Drive pushes the input, Bias shifts the operating point, "
            "Tone tilts the post-shape EQ, Mix blends dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaSaturatorCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
