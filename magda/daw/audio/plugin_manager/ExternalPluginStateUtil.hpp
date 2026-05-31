#pragma once

#include <tracktion_engine/tracktion_engine.h>
// Internal TE header: ExternalAutomatableParameter is not exposed via the module
// umbrella, but its public valueChangedByPlugin() is how we refresh TE's parameter
// cache from a synth after restoring its native state chunk.
#include <tracktion_engine/plugins/external/tracktion_ExternalAutomatableParameter.h>

namespace magda {

/**
 * Make an external plugin's native state chunk authoritative over its saved
 * per-parameter array.
 *
 * For a VST/AU the entire voice lives in the native state chunk (pluginState) --
 * it already encodes every parameter. Restoring a device also re-applies the
 * saved per-parameter array (syncFromDeviceInfo), which corrupts the restored
 * voice whenever that array is stale relative to the chunk (e.g. parameters
 * captured during an earlier buggy load, before the param cache tracked the
 * restored voice). Call this AFTER the parameter sync so the chunk wins: it
 * re-asserts the chunk, then refreshes TE's AutomatableParameter cache from the
 * live plugin so the later playback-graph build can't write stale cached values
 * back over the voice.
 *
 * No-ops for internal plugins and for an empty chunk. Callers should re-capture
 * DeviceInfo::parameters (populateParameters) afterwards so the canonical model
 * reflects the restored voice, breaking the stale-array cycle.
 *
 * (The async load path avoids this entirely: it only reads parameters back, never
 * writes the saved array, so the restored chunk is never touched.)
 */
inline void reassertExternalPluginChunk(tracktion::engine::Plugin* plugin,
                                        const juce::String& pluginState) {
    if (pluginState.isEmpty())
        return;
    auto* ext = dynamic_cast<tracktion::engine::ExternalPlugin*>(plugin);
    if (ext == nullptr)
        return;
    ext->state.setProperty(tracktion::engine::IDs::state, pluginState, nullptr);
    ext->restorePluginStateFromValueTree(ext->state);
    for (auto* p : ext->getAutomatableParameters())
        if (auto* ep = dynamic_cast<tracktion::engine::ExternalAutomatableParameter*>(p))
            ep->valueChangedByPlugin();
}

}  // namespace magda
