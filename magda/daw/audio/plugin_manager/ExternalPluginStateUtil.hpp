#pragma once

// clang-format off
// Order is load-bearing and MUST NOT be sorted: the internal header
// tracktion_ExternalAutomatableParameter.h is not self-contained (it needs
// juce::AudioProcessorParameter + AutomatableParameter from the umbrella) and is
// not exposed via the module umbrella. With SortIncludes on, clang-format would
// sort "plugins/..." ahead of "tracktion_engine.h" and break the build, so this
// block is fenced off. We need the internal header for the public
// valueChangedByPlugin(), which refreshes TE's parameter cache after a restore.
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_engine/plugins/external/tracktion_ExternalAutomatableParameter.h>
// clang-format on

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
 * This is the final step of the baseline -> overlay -> refresh restore sequence
 * (see restoreDeviceStateWithChunkOverlay in PluginManagerSync.cpp):
 * syncFromDeviceInfo applies the saved parameter array as a baseline, the native
 * chunk is applied as the authoritative overlay, then this refreshes TE's cache
 * to match the resulting plugin state.
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

/**
 * Apply a base64 .vstpreset (Steinberg preset format) to a VST3 via
 * ExtensionsVisitor::VST3Client::setPreset(), then sync TE's parameter cache.
 * Used for state imported from DAWproject, where the portable state is a
 * .vstpreset rather than MAGDA's TE chunk. Requires the instance to be live
 * (call after async load completes). Returns true if applied; no-op/false for
 * non-VST3, not-yet-live, or undecodable data.
 */
inline bool applyVst3Preset(tracktion::engine::Plugin* plugin, const juce::String& presetBase64) {
    if (presetBase64.isEmpty())
        return false;
    auto* ext = dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin);
    if (ext == nullptr || ext->isInitialisingAsync() || ext->getAudioPluginInstance() == nullptr)
        return false;

    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, presetBase64))
        return false;

    struct ApplyVisitor : juce::ExtensionsVisitor {
        juce::MemoryBlock data;
        bool ok = false;
        void visitVST3Client(const VST3Client& client) override {
            ok = client.setPreset(data);
        }
    };
    ApplyVisitor visitor;
    visitor.data = decoded.getMemoryBlock();
    ext->getAudioPluginInstance()->getExtensions(visitor);
    if (!visitor.ok)
        return false;  // not a VST3 instance / apply failed

    // Mirror the applied state into TE's state property so a later graph build /
    // resync doesn't revert it, then refresh the parameter cache to match.
    ext->flushPluginStateToValueTree();
    refreshExternalPluginParameterCache(ext);
    return true;
}

}  // namespace magda
