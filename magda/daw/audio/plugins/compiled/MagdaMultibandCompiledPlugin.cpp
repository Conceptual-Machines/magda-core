#include "plugins/compiled/MagdaMultibandCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"amount", 0, "Amount"},
    {"attack", 1, "Attack"},
    {"release", 2, "Release"},
    {"input", 3, "Input"},
    {"output", 4, "Output"},
    {"mix", 5, "Mix"},
    {"low_input", 6, "Low Input"},
    {"mid_input", 7, "Mid Input"},
    {"high_input", 8, "High Input"},
    {"low_gain", 9, "Low Output"},
    {"mid_gain", 10, "Mid Output"},
    {"high_gain", 11, "High Output"},
    {"low_lower_threshold", 12, "Low Lower Threshold"},
    {"low_upper_threshold", 13, "Low Upper Threshold"},
    {"low_below_ratio", 14, "Low Below Ratio"},
    {"low_above_ratio", 15, "Low Above Ratio"},
    {"low_range", 16, "Low Range"},
    {"low_limit", 17, "Low Limit"},
    {"low_attack", 18, "Low Attack"},
    {"low_release", 19, "Low Release"},
    {"mid_lower_threshold", 20, "Mid Lower Threshold"},
    {"mid_upper_threshold", 21, "Mid Upper Threshold"},
    {"mid_below_ratio", 22, "Mid Below Ratio"},
    {"mid_above_ratio", 23, "Mid Above Ratio"},
    {"mid_range", 24, "Mid Range"},
    {"mid_limit", 25, "Mid Limit"},
    {"mid_attack", 26, "Mid Attack"},
    {"mid_release", 27, "Mid Release"},
    {"high_lower_threshold", 28, "High Lower Threshold"},
    {"high_upper_threshold", 29, "High Upper Threshold"},
    {"high_below_ratio", 30, "High Below Ratio"},
    {"high_above_ratio", 31, "High Above Ratio"},
    {"high_range", 32, "High Range"},
    {"high_limit", 33, "High Limit"},
    {"high_attack", 34, "High Attack"},
    {"high_release", 35, "High Release"},
    {"low_xo", 36, "Low XO"},
    {"high_xo", 37, "High XO"},
};

const CompiledPluginSpec& getMagdaMultibandSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaMultibandCompiledPlugin::xmlTypeName,
        .displayName = "Multiband Dynamics",
        .browserCategory = "Dynamics",
        .description =
            "Native 3-band dynamics processor with independent lower and upper threshold "
            "regions per band. Ratios above 1:1 compress toward the active threshold; "
            "ratios below 1:1 expand away from it.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaMultibandCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
