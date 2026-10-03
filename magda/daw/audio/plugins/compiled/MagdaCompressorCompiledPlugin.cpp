#include "plugins/compiled/MagdaCompressorCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},      {"threshold", 1, "Threshold"},
    {"ratio", 2, "Ratio"},        {"attack", 3, "Attack"},
    {"release", 4, "Release"},    {"knee", 5, "Knee"},
    {"makeup", 6, "Makeup"},      {"mix", 7, "Mix"},
    {"output", 8, "Output"},      {"detector", 9, "Detector"},
    {"link", 10, "Link"},         {"sc_hpf", 11, "SC HPF"},
    {"fbff", 12, "FBFF"},         {"style", 13, "Style"},
    {"autogain", 14, "Autogain"},
};

// Tracktion's retired Compressor loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"compressor"};

const CompiledPluginSpec& getMagdaCompressorSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaCompressorCompiledPlugin::xmlTypeName,
        .displayName = "Compressor",
        .browserCategory = "Dynamics",
        .description =
            "Compiled Faust compressor with selectable engines.\n"
            "<b>Clean</b>: feed-forward, peak/RMS detection, soft knee, stereo link, "
            "sidechain HPF, external audio sidechain, parallel mix, output safety limiting.\n"
            "<b>Glue</b>: Brouns FBFF compressor with exposed character controls "
            "(Detector Peak/RMS, Style Pre/Post, FBFF blend). No external sidechain.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaCompressorCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
