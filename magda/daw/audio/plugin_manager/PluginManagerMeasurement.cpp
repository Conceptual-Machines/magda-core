// Per-track measurement tap lifecycle (issue #1388).
//
// Inserts/removes the always-on TrackMeasurementPlugin that the Levels meter
// and the mixing agent read from. Kept separate from the sidechain/sync/
// modifier partial-class files: measurement is its own concern. The higher-
// level enablement policy, polling and snapshot API live in
// TrackMeasurementManager, which drives these through the audio bridge.

#include "../../core/TypeIds.hpp"
#include "../TrackController.hpp"
#include "PluginManager.hpp"
#include "plugins/TrackMeasurementPlugin.hpp"

namespace magda {

namespace {

// Resolve the plugin list to tap: the master's list for MASTER_TRACK_ID,
// otherwise the track's own chain. Returns nullptr if the track has no TE track.
te::PluginList* measurementPluginList(te::Edit& edit, TrackController& trackController,
                                      TrackId trackId) {
    if (trackId == MASTER_TRACK_ID)
        return &edit.getMasterPluginList();
    if (auto* teTrack = trackController.getAudioTrack(trackId))
        return &teTrack->pluginList;
    return nullptr;
}

}  // namespace

daw::audio::TrackMeasurementPlugin* PluginManager::ensureTrackMeasurementTap(TrackId trackId) {
    // Already tracked? Return the cached instance.
    if (auto it = trackMeasurementTaps_.find(trackId); it != trackMeasurementTaps_.end())
        return dynamic_cast<daw::audio::TrackMeasurementPlugin*>(it->second.get());

    auto* list = measurementPluginList(edit_, trackController_, trackId);
    if (list == nullptr)
        return nullptr;

    // Adopt an existing tap if one is already on the chain (e.g. after restore).
    for (int i = 0; i < list->size(); ++i) {
        if (auto* existing = dynamic_cast<daw::audio::TrackMeasurementPlugin*>((*list)[i])) {
            trackMeasurementTaps_[trackId] = (*list)[i];
            return existing;
        }
    }

    const bool isMaster = (trackId == MASTER_TRACK_ID);
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, daw::audio::TrackMeasurementPlugin::xmlTypeName,
                            nullptr);
    // True-peak oversampling is the heavy path, so only the master runs it; per-
    // track taps stay on sample peak (set via the persisted property pre-create).
    if (isMaster)
        pluginState.setProperty(juce::Identifier("measureTruePeak"), true, nullptr);

    auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    if (!plugin)
        return nullptr;

    // Post-fader: append at the very end of the chain (after volume/pan and TE's
    // level meter) so the tap measures what actually sums downstream.
    list->insertPlugin(plugin, list->size(), nullptr);
    trackMeasurementTaps_[trackId] = plugin;
    return dynamic_cast<daw::audio::TrackMeasurementPlugin*>(plugin.get());
}

void PluginManager::removeTrackMeasurementTap(TrackId trackId) {
    auto it = trackMeasurementTaps_.find(trackId);
    if (it == trackMeasurementTaps_.end())
        return;

    auto* plugin = it->second.get();
    trackMeasurementTaps_.erase(it);
    if (plugin)
        plugin->deleteFromParent();
}

daw::audio::TrackMeasurementPlugin* PluginManager::getTrackMeasurementTap(TrackId trackId) const {
    auto it = trackMeasurementTaps_.find(trackId);
    if (it == trackMeasurementTaps_.end())
        return nullptr;
    return dynamic_cast<daw::audio::TrackMeasurementPlugin*>(it->second.get());
}

}  // namespace magda
