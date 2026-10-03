#include "plugins/compiled/MagdaEqCompiledPlugin.hpp"

#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

constexpr AliasSpec kAliases[] = {
    {"band1_enabled", 0, "Band 1 Enabled"},
    {"band1_type", 1, "Band 1 Type"},
    {"band1_freq", 2, "Band 1 Freq"},
    {"band1_gain", 3, "Band 1 Gain"},
    {"band1_q", 4, "Band 1 Q"},
    {"band2_enabled", 5, "Band 2 Enabled"},
    {"band2_type", 6, "Band 2 Type"},
    {"band2_freq", 7, "Band 2 Freq"},
    {"band2_gain", 8, "Band 2 Gain"},
    {"band2_q", 9, "Band 2 Q"},
    {"band3_enabled", 10, "Band 3 Enabled"},
    {"band3_type", 11, "Band 3 Type"},
    {"band3_freq", 12, "Band 3 Freq"},
    {"band3_gain", 13, "Band 3 Gain"},
    {"band3_q", 14, "Band 3 Q"},
    {"band4_enabled", 15, "Band 4 Enabled"},
    {"band4_type", 16, "Band 4 Type"},
    {"band4_freq", 17, "Band 4 Freq"},
    {"band4_gain", 18, "Band 4 Gain"},
    {"band4_q", 19, "Band 4 Q"},
    {"band5_enabled", 20, "Band 5 Enabled"},
    {"band5_type", 21, "Band 5 Type"},
    {"band5_freq", 22, "Band 5 Freq"},
    {"band5_gain", 23, "Band 5 Gain"},
    {"band5_q", 24, "Band 5 Q"},
    {"band6_enabled", 25, "Band 6 Enabled"},
    {"band6_type", 26, "Band 6 Type"},
    {"band6_freq", 27, "Band 6 Freq"},
    {"band6_gain", 28, "Band 6 Gain"},
    {"band6_q", 29, "Band 6 Q"},
    {"band7_enabled", 30, "Band 7 Enabled"},
    {"band7_type", 31, "Band 7 Type"},
    {"band7_freq", 32, "Band 7 Freq"},
    {"band7_gain", 33, "Band 7 Gain"},
    {"band7_q", 34, "Band 7 Q"},
    {"band8_enabled", 35, "Band 8 Enabled"},
    {"band8_type", 36, "Band 8 Type"},
    {"band8_freq", 37, "Band 8 Freq"},
    {"band8_gain", 38, "Band 8 Gain"},
    {"band8_q", 39, "Band 8 Q"},
    {"output", 40, "Output"},
};

// Tracktion's retired 4-band EQ loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"4bandEq", "eq", "equaliser"};

const CompiledPluginSpec& getMagdaEqSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaEqCompiledPlugin::xmlTypeName,
        .displayName = "EQ",
        .browserCategory = "EQ",
        .description = "Built-in 8-band parametric equaliser. Per-band Enabled skips inactive "
                       "biquads, and double-clicking a band's dot on the curve toggles it; "
                       "Type selects "
                       "<b>HP</b>, <b>LowShelf</b>, <b>Bell</b>, <b>HighShelf</b>, "
                       "<b>LP</b>, or <b>Notch</b>. "
                       "MAGDA-owned RBJ biquads drive audio and share coefficient math with "
                       "the curve view. "
                       "Each band exposes Freq, Gain, Q; Output trims the final sum.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaEqCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
