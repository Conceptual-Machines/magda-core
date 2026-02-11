#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../core/DeviceInfo.hpp"
#include "../core/RackInfo.hpp"
#include "../core/TypeIds.hpp"
#include "CurveSnapshot.hpp"

namespace magda {

namespace te = tracktion;

class PluginManager;

/**
 * @brief Manages MAGDA RackInfo → TE RackType audio routing
 *
 * Maps each MAGDA RackId to a TE RackType + RackInstance. When a rack appears
 * in a track's chain elements, this manager creates the TE audio graph:
 * - Each chain becomes a parallel signal path inside the RackType
 * - Chain elements are loaded as plugins inside the rack
 * - Serial connections within each chain, parallel chains summed at output
 * - Per-chain VolumeAndPan for volume/pan/mute/solo
 * - Rack bypass via RackInstance wet/dry gains
 *
 * Follows the InstrumentRackManager pattern for TE RackType lifecycle.
 */
class RackSyncManager {
  public:
    RackSyncManager(te::Edit& edit, PluginManager& pluginManager);

    /**
     * @brief Create TE RackType from MAGDA RackInfo and return RackInstance to insert on track
     * @param trackId The MAGDA track ID (for plugin creation context)
     * @param rackInfo The MAGDA rack data model
     * @return The RackInstance plugin to insert on the TE track, or nullptr on failure
     */
    te::Plugin::Ptr syncRack(TrackId trackId, const RackInfo& rackInfo);

    /**
     * @brief Rebuild rack connections when chain elements change
     * @param trackId The MAGDA track ID
     * @param rackInfo Updated rack data model
     */
    void resyncRack(TrackId trackId, const RackInfo& rackInfo);

    /**
     * @brief Clean up RackType, RackInstance, and inner plugins for a rack
     * @param rackId The MAGDA rack ID to remove
     */
    void removeRack(RackId rackId);

    /**
     * @brief Find a plugin inside any synced rack (for parameter access)
     * @param deviceId The MAGDA device ID of the inner plugin
     * @return The TE plugin, or nullptr if not found
     */
    te::Plugin* getInnerPlugin(DeviceId deviceId) const;

    /**
     * @brief Check if a TE plugin is one of our RackInstances
     * @param plugin The TE plugin to check
     * @return true if this is a RackInstance managed by us
     */
    bool isRackInstance(te::Plugin* plugin) const;

    /**
     * @brief Get the RackId associated with a RackInstance plugin
     * @param plugin The RackInstance plugin
     * @return The RackId, or INVALID_RACK_ID if not found
     */
    RackId getRackIdForInstance(te::Plugin* plugin) const;

    /**
     * @brief Clear all synced rack state (for shutdown)
     */
    void clear();

    // =========================================================================
    // Phase 2: Macro/Modulator Integration
    // =========================================================================

    /**
     * @brief Set a macro parameter value on the TE RackType
     * @param rackId The MAGDA rack ID
     * @param macroIndex Index into the rack's macro array
     * @param value Normalized value (0.0 to 1.0)
     */
    void setMacroValue(RackId rackId, int macroIndex, float value);

    /**
     * @brief Resync only modifiers for all racks on a track (full rebuild)
     */
    void resyncAllModifiers(TrackId trackId);

    /**
     * @brief Update existing modifier properties in-place (rate, waveform, sync)
     * without destroying/recreating modifiers
     */
    void updateAllModifierProperties(TrackId trackId);

    /**
     * @brief Trigger note-on resync on all TE LFO modifiers inside racks on a track
     * Thread-safe: can be called from MIDI thread.
     */
    void triggerLFONoteOn(TrackId trackId);

    /**
     * @brief Collect all te::LFOModifier* from racks on a given track
     * Used by PluginManager::rebuildSidechainLFOCache() to populate the cache.
     */
    void collectLFOModifiers(TrackId trackId, std::vector<te::LFOModifier*>& out) const;

    /**
     * @brief Check if any rack on a track needs a full modifier resync
     *
     * Compares the number of active rack mods (enabled + has links) against
     * the number of existing TE modifiers in innerModifiers. Returns true
     * if there's a mismatch, meaning new modifiers need to be created or
     * old ones removed.
     */
    bool needsModifierResync(TrackId trackId) const;

  private:
    /**
     * @brief Internal state for a synced rack
     */
    struct SyncedRack {
        RackId rackId = INVALID_RACK_ID;
        TrackId trackId = INVALID_TRACK_ID;
        te::RackType::Ptr rackType;
        te::Plugin::Ptr rackInstance;                           // RackInstance on the TE track
        std::map<DeviceId, te::Plugin::Ptr> innerPlugins;       // plugins inside the rack
        std::map<ChainId, te::Plugin::Ptr> chainVolPanPlugins;  // per-chain VolumeAndPan

        // Phase 2: modulation
        std::map<ModId, te::Modifier::Ptr> innerModifiers;
        std::map<int, te::MacroParameter*> innerMacroParams;  // index → TE MacroParameter

        // Double-buffered curve snapshots for custom LFO waveforms (keyed by ModId)
        std::unordered_map<int, std::unique_ptr<CurveSnapshotHolder>> curveSnapshots;
    };

    /**
     * @brief Check if rack structure changed (devices/chains added/removed/reordered)
     * vs only properties changed (bypass, volume, mute/solo)
     */
    bool structureChanged(const SyncedRack& synced, const RackInfo& rackInfo) const;

    /**
     * @brief Update only properties (bypass, volume, chain mute/solo) without rebuilding plugins
     */
    void updateProperties(SyncedRack& synced, const RackInfo& rackInfo);

    /**
     * @brief Build connections within the RackType from RackInfo chains
     */
    void buildConnections(SyncedRack& synced, const RackInfo& rackInfo);

    /**
     * @brief Load chain elements as plugins into the RackType
     */
    void loadChainPlugins(SyncedRack& synced, TrackId trackId, const RackInfo& rackInfo);

    /**
     * @brief Create a plugin from DeviceInfo without inserting onto a track
     * (delegates to PluginManager::createPluginOnly)
     */
    te::Plugin::Ptr createPluginForRack(TrackId trackId, const DeviceInfo& device);

    /**
     * @brief Sync TE modifiers from RackInfo mods (Phase 2)
     */
    void syncModifiers(SyncedRack& synced, const RackInfo& rackInfo);

    /**
     * @brief Sync TE macro parameters from RackInfo macros (Phase 2)
     */
    void syncMacros(SyncedRack& synced, const RackInfo& rackInfo);

    /**
     * @brief Apply rack bypass state via wet/dry gains
     */
    void applyBypassState(SyncedRack& synced, const RackInfo& rackInfo);

    te::Edit& edit_;
    PluginManager& pluginManager_;

    std::map<RackId, SyncedRack> syncedRacks_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackSyncManager)
};

}  // namespace magda
