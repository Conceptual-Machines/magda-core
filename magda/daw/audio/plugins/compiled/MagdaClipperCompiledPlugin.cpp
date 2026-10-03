#include "plugins/compiled/MagdaClipperCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"drive", 0, "Drive"},
    {"mode", 1, "Mode"},
    {"output", 2, "Output"},
};

const CompiledPluginSpec& getMagdaClipperSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaClipperCompiledPlugin::xmlTypeName,
        .displayName = "Clipper",
        .browserCategory = "Distortion",
        .description = "Compiled Faust antialiased clipper with five selectable static curves "
                       "from the aa.* ADAA library.\n"
                       "<b>Hard</b>: brickwall clip ceiling.\n"
                       "<b>Soft</b>: quadratic knee for warmer breakup.\n"
                       "<b>Tanh</b>: hyperbolic tube-style curve.\n"
                       "<b>Hyperbolic</b>: smooth rational saturation.\n"
                       "<b>Sine</b>: sin(atan(x)) for asymmetric, harmonically rich clipping.\n"
                       "All five are instantiated in parallel for glitch-free Mode switching. "
                       "Drive pushes the input into the curve; Output trims the result.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaClipperCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
