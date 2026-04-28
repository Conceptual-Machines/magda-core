#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "../daw/core/DeviceInfo.hpp"
#include "../daw/core/PluginAlias.hpp"

namespace magda {

/**
 * @brief Single source of truth for MAGDA's built-in (non-scanned) plugins
 *        addressable from agent code and the autocomplete dropdown.
 *
 * The canonical alias for each entry is `pluginNameToAlias(displayName)`,
 * matching what the plugin browser / autocomplete UI suggests for external
 * plugins. Don't add variants — when the user types an alias, autocomplete
 * already steers them to the canonical form.
 */
struct InternalPluginInfo {
    juce::String displayName;
    juce::String pluginId;
    DeviceType deviceType;
};

/// Built-in MAGDA + Tracktion plugins exposed to the agent layer + autocomplete.
inline const std::vector<InternalPluginInfo>& getInternalPlugins() {
    static const std::vector<InternalPluginInfo> kPlugins = {
        // Effects
        {"Equaliser", "eq", DeviceType::Effect},
        {"Compressor", "compressor", DeviceType::Effect},
        {"Reverb", "reverb", DeviceType::Effect},
        {"Delay", "delay", DeviceType::Effect},
        {"Chorus", "chorus", DeviceType::Effect},
        {"Phaser", "phaser", DeviceType::Effect},
        {"Filter", "lowpass", DeviceType::Effect},
        {"Utility", "utility", DeviceType::Effect},
        {"Pitch Shift", "pitchshift", DeviceType::Effect},
        {"IR Reverb", "impulseresponse", DeviceType::Effect},
        {"Test Tone", "tone", DeviceType::Effect},
        // Instruments
        {"4OSC Synth", "4osc", DeviceType::Instrument},
        {"MAGDA Sampler", "magdasampler", DeviceType::Instrument},
        {"Drum Grid", "drumgrid", DeviceType::Instrument},
    };
    return kPlugins;
}

/**
 * @brief Look an internal plugin up by its canonical alias.
 *
 * The match is case-insensitive against `pluginNameToAlias(displayName)`
 * for each registered plugin. Returns nullptr when no plugin matches —
 * caller should fall through to the external KnownPluginList lookup.
 */
inline const InternalPluginInfo* lookupInternalPluginByAlias(const juce::String& alias) {
    for (const auto& entry : getInternalPlugins()) {
        if (pluginNameToAlias(entry.displayName).equalsIgnoreCase(alias))
            return &entry;
    }
    return nullptr;
}

}  // namespace magda
