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
/// Strongly-typed identifier for MAGDA's built-in plugins. Use this in
/// switch statements / dispatch tables instead of comparing pluginId
/// strings — the enum makes "did I cover all internal devices?" a
/// compiler-checked question and removes the string-typo risk.
enum class InternalPlugin {
    None,  // not an internal plugin (external VST/AU, or unknown id)
    Equaliser,
    Compressor,
    Reverb,
    Delay,
    Chorus,
    Phaser,
    Filter,
    Utility,
    PitchShift,
    ImpulseResponse,
    TestTone,
    FourOsc,
    MagdaSampler,
    DrumGrid,
};

struct InternalPluginInfo {
    juce::String displayName;
    juce::String pluginId;
    DeviceType deviceType;
    InternalPlugin id = InternalPlugin::None;
};

/// Built-in MAGDA + Tracktion plugins exposed to the agent layer + autocomplete.
inline const std::vector<InternalPluginInfo>& getInternalPlugins() {
    static const std::vector<InternalPluginInfo> kPlugins = {
        // Effects
        {"Equaliser", "eq", DeviceType::Effect, InternalPlugin::Equaliser},
        {"Compressor", "compressor", DeviceType::Effect, InternalPlugin::Compressor},
        {"Reverb", "reverb", DeviceType::Effect, InternalPlugin::Reverb},
        {"Delay", "delay", DeviceType::Effect, InternalPlugin::Delay},
        {"Chorus", "chorus", DeviceType::Effect, InternalPlugin::Chorus},
        {"Phaser", "phaser", DeviceType::Effect, InternalPlugin::Phaser},
        {"Filter", "lowpass", DeviceType::Effect, InternalPlugin::Filter},
        {"Utility", "utility", DeviceType::Effect, InternalPlugin::Utility},
        {"Pitch Shift", "pitchshift", DeviceType::Effect, InternalPlugin::PitchShift},
        {"IR Reverb", "impulseresponse", DeviceType::Effect, InternalPlugin::ImpulseResponse},
        {"Test Tone", "tone", DeviceType::Effect, InternalPlugin::TestTone},
        // Instruments
        {"4OSC Synth", "4osc", DeviceType::Instrument, InternalPlugin::FourOsc},
        {"MAGDA Sampler", "magdasampler", DeviceType::Instrument, InternalPlugin::MagdaSampler},
        {"Drum Grid", "drumgrid", DeviceType::Instrument, InternalPlugin::DrumGrid},
    };
    return kPlugins;
}

/**
 * @brief Resolve a pluginId string to the strongly-typed InternalPlugin enum.
 *
 * Match is case-insensitive against the pluginId column above. Returns
 * InternalPlugin::None for external plugins, unknown ids, or empty input.
 * Use this at dispatch points instead of `equalsIgnoreCase("4osc")`.
 */
inline InternalPlugin internalPluginFromId(const juce::String& pluginId) {
    if (pluginId.isEmpty())
        return InternalPlugin::None;
    for (const auto& entry : getInternalPlugins()) {
        if (entry.pluginId.equalsIgnoreCase(pluginId))
            return entry.id;
    }
    return InternalPlugin::None;
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
