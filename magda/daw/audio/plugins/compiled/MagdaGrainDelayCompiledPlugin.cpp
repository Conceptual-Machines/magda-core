#include "plugins/compiled/MagdaGrainDelayCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"time", 0, "Time"},         {"division", 1, "Division"}, {"sync", 2, "Sync"},
    {"size", 3, "Size"},         {"pitch", 4, "Pitch"},       {"spray", 5, "Spray"},
    {"feedback", 6, "Feedback"}, {"mix", 7, "Mix"},
};

const CompiledPluginSpec& getMagdaGrainDelaySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGrainDelayCompiledPlugin::xmlTypeName,
        .displayName = "Grain Delay",
        .browserCategory = "Delay",
        .description =
            "Compiled Faust granular delay. A feedback delay line is read through a 4-voice "
            "Hann-windowed grain bank with 25% overlap. "
            "Pitch shifts via per-grain read-offset drift; Spray jitters the per-grain position. "
            "Time spans the base delay, locking to musical Division when Sync is on. "
            "Feedback recirculates through the grain bank; Mix blends wet against dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaGrainDelayCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
