#include "InternalPluginAliases.hpp"

#include "audio/plugins/InternalPluginRegistry.hpp"

namespace magda {

// Every internal device that curates parameter aliases now does so next to
// itself: compiled devices declare an `AliasSpec` table in their wrapper, which
// the base device pack registers into the plugin registry. This used to hold a
// hand-written table for the stock Tracktion effects (EQ, Compressor, Reverb,
// Delay, Chorus, Phaser, Pitch Shift, Lowpass) because those had no such table
// of their own; they have since been retired in favour of their compiled
// successors, so nothing is authored here any more.
//
// Devices still without curated names (4OSC, Sampler, DrumGrid, Arpeggiator,
// StepSequencer, IR Reverb, Tone Generator) fall back to AutoGen, whose names
// are already user-readable, so the chained @plugin.param popup works without
// curated entries.
std::map<juce::String, StoredAlias> collectInternalPluginCuratedAliases() {
    std::map<juce::String, StoredAlias> out;

    for (const auto& registered : daw::audio::getAllInternalParameterAliases()) {
        StoredAlias alias;
        alias.pluginTypeKey = registered.pluginKey;
        alias.paramIndex = registered.paramIndex;
        alias.paramNameAtSetTime = registered.paramName;
        alias.path = std::nullopt;  // Resolved at runtime.

        out[juce::String(registered.pluginKey) + "." + registered.alias] = alias;
    }

    return out;
}

}  // namespace magda
