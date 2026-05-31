#pragma once

#include <tracktion_engine/tracktion_engine.h>
// Internal TE header: ExternalAutomatableParameter is not exposed via the module
// umbrella, but its public valueChangedByPlugin() is how we refresh TE's parameter
// cache from a synth after restoring its native state chunk.
#include <tracktion_engine/plugins/external/tracktion_ExternalAutomatableParameter.h>

namespace magda {

/**
 * Sync TE's AutomatableParameter cache for an external plugin to the plugin's
 * current (already-restored) parameter values.
 *
 * For a VST/AU the entire voice lives in the native state chunk; restoring it via
 * setStateInformation updates the plugin but NOT TE's per-parameter cache, which
 * was read at plugin construction (the default/INIT voice). When the playback
 * graph is later built TE writes that cache back onto the plugin, reverting the
 * restored voice. Calling this after the chunk is restored makes the cache agree
 * with the plugin so the graph build is a no-op. No-op for internal plugins / no
 * instance.
 *
 * This is the single source of truth for the parameter clobber: it does NOT
 * depend on call ordering relative to ExternalPluginProcessor::syncFromDeviceInfo,
 * because syncFromDeviceInfo deliberately does not write the saved per-parameter
 * array while a chunk is present (see ExternalPluginProcessor.cpp).
 */
inline void refreshExternalPluginParameterCache(tracktion::engine::Plugin* plugin) {
    auto* ext = dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin);
    if (ext == nullptr)
        return;
    // Only meaningful once the instance is live. Before async instantiation
    // completes there are no ExternalAutomatableParameters to refresh, and TE
    // rebuilds + refreshes the cache itself on completion
    // (completePluginInstanceCreation). Guard explicitly, matching createPluginOnly.
    if (ext->isInitialisingAsync() || ext->getAudioPluginInstance() == nullptr)
        return;
    for (auto* p : ext->getAutomatableParameters())
        if (auto* ep = dynamic_cast<tracktion::engine::ExternalAutomatableParameter*>(p))
            ep->valueChangedByPlugin();
}

/**
 * Apply a saved native-state chunk to an external plugin and sync TE's parameter
 * cache to it. Use when (re)applying authoritative state, e.g. loading a device
 * preset. No-op for internal plugins / empty chunk.
 */
inline void applyExternalPluginChunk(tracktion::engine::Plugin* plugin, const juce::String& chunk) {
    if (chunk.isEmpty())
        return;
    auto* ext = dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin);
    if (ext == nullptr)
        return;
    // Always publish the chunk on the state property: TE reads it during async
    // instantiation, so this is how an async plugin receives its state.
    ext->state.setProperty(tracktion::engine::IDs::state, chunk, nullptr);
    // Apply + refresh only once the instance is live (matches createPluginOnly's
    // guard); for an async plugin TE applies the property itself on completion.
    if (ext->isInitialisingAsync() || ext->getAudioPluginInstance() == nullptr)
        return;
    ext->restorePluginStateFromValueTree(ext->state);
    refreshExternalPluginParameterCache(ext);
}

}  // namespace magda
