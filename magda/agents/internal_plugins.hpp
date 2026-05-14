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
    Arpeggiator,
    MidiChordEngine,
    StepSequencer,
    Faust,
    Mod,
    Flanger,
    RingMod,
    FreqShift,
    Limiter,
    Clipper,
};

/// Vendor of an internal plugin. TracktionEngine = stock TE plugin (gets TE
/// branding in the UI). Magda = MAGDA-native plugin we wrote ourselves.
enum class InternalPluginVendor {
    TracktionEngine,
    Magda,
};

struct InternalPluginInfo {
    juce::String displayName;
    juce::String pluginId;
    DeviceType deviceType;
    InternalPlugin id = InternalPlugin::None;
    InternalPluginVendor vendor = InternalPluginVendor::TracktionEngine;
};

/// Built-in MAGDA + Tracktion plugins exposed to the agent layer + autocomplete.
inline const std::vector<InternalPluginInfo>& getInternalPlugins() {
    using V = InternalPluginVendor;
    static const std::vector<InternalPluginInfo> kPlugins = {
        // Effects (TE stock)
        {"Equaliser", "eq", DeviceType::Effect, InternalPlugin::Equaliser, V::TracktionEngine},
        {"Compressor", "magda_compressor", DeviceType::Effect, InternalPlugin::Compressor,
         V::Magda},
        {"Reverb", "reverb", DeviceType::Effect, InternalPlugin::Reverb, V::TracktionEngine},
        {"Delay", "delay", DeviceType::Effect, InternalPlugin::Delay, V::TracktionEngine},
        {"Chorus", "magda_chorus", DeviceType::Effect, InternalPlugin::Chorus, V::Magda},
        {"Filter", "lowpass", DeviceType::Effect, InternalPlugin::Filter, V::TracktionEngine},
        {"Utility", "utility", DeviceType::Effect, InternalPlugin::Utility, V::TracktionEngine},
        {"Pitch Shift", "pitchshift", DeviceType::Effect, InternalPlugin::PitchShift,
         V::TracktionEngine},
        {"IR Reverb", "impulseresponse", DeviceType::Effect, InternalPlugin::ImpulseResponse,
         V::TracktionEngine},
        {"Test Tone", "tone", DeviceType::Effect, InternalPlugin::TestTone, V::TracktionEngine},
        // Effects (MAGDA-native)
        {"Phaser", "magda_phaser", DeviceType::Effect, InternalPlugin::Phaser, V::Magda},
        {"Mod", "magda_mod", DeviceType::Effect, InternalPlugin::Mod, V::Magda},
        {"Flanger", "magda_flanger", DeviceType::Effect, InternalPlugin::Flanger, V::Magda},
        {"Ring Mod", "magda_ring_mod", DeviceType::Effect, InternalPlugin::RingMod, V::Magda},
        {"Freq Shift", "magda_freq_shift", DeviceType::Effect, InternalPlugin::FreqShift, V::Magda},
        {"Limiter", "magda_limiter", DeviceType::Effect, InternalPlugin::Limiter, V::Magda},
        {"Clipper", "magda_clipper", DeviceType::Effect, InternalPlugin::Clipper, V::Magda},
        {"Faust", "faust", DeviceType::Effect, InternalPlugin::Faust, V::Magda},
        // Instruments (TE stock)
        {"4OSC Synth", "4osc", DeviceType::Instrument, InternalPlugin::FourOsc, V::TracktionEngine},
        // Instruments (MAGDA-native)
        {"MAGDA Sampler", "magdasampler", DeviceType::Instrument, InternalPlugin::MagdaSampler,
         V::Magda},
        {"Drum Grid", "drumgrid", DeviceType::Instrument, InternalPlugin::DrumGrid, V::Magda},
        // MIDI processors (MAGDA-native)
        {"Arpeggiator", "arpeggiator", DeviceType::Effect, InternalPlugin::Arpeggiator, V::Magda},
        {"Chord Engine", "midichordengine", DeviceType::Effect, InternalPlugin::MidiChordEngine,
         V::Magda},
        {"Step Sequencer", "stepsequencer", DeviceType::Effect, InternalPlugin::StepSequencer,
         V::Magda},
    };
    return kPlugins;
}

/// True iff `pluginId` matches a stock Tracktion Engine plugin (the kind that
/// should display the TE brand mark). Returns false for MAGDA-native built-ins
/// (DrumGrid, MAGDA Sampler, Faust, …) and for external VST/AU plugins.
inline bool isTracktionEngineStockPlugin(const juce::String& pluginId) {
    if (pluginId.isEmpty())
        return false;
    if (pluginId.equalsIgnoreCase("compressor"))
        return true;  // legacy TE compressor projects
    for (const auto& entry : getInternalPlugins()) {
        if (entry.pluginId.equalsIgnoreCase(pluginId))
            return entry.vendor == InternalPluginVendor::TracktionEngine;
    }
    return false;
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
