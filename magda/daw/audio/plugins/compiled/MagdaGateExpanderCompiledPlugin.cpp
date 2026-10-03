#include "plugins/compiled/MagdaGateExpanderCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"attack", 0, "Attack"}, {"release", 1, "Release"},     {"mix", 2, "Mix"},
    {"output", 3, "Output"}, {"threshold", 4, "Threshold"}, {"ratio", 5, "Ratio"},
    {"range", 6, "Range"},
};

const CompiledPluginSpec& getMagdaGateExpanderSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGateExpanderCompiledPlugin::xmlTypeName,
        .displayName = "Gate",
        .browserCategory = "Dynamics",
        .description =
            "Compiled Faust stereo gate / downward expander with a linked peak detector. "
            "Threshold sets where the gate opens; Ratio shapes the slope; "
            "Range bounds the deepest cut. "
            "Attack and Release shape the envelope; "
            "Mix blends the gated signal back against dry for parallel gating.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaGateExpanderCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
