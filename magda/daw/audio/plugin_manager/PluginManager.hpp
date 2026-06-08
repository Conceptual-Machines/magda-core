#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "../../core/ChainNodePath.hpp"
#include "../../core/DeviceInfo.hpp"
#include "../../core/SelectionManager.hpp"
#include "../../core/TypeIds.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "processors/base/DeviceProcessor.hpp"
#include "racks/InstrumentRackManager.hpp"
#include "racks/RackSyncManager.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;
struct TrackInfo;
class TrackController;
class PluginWindowBridge;
class TransportStateManager;
namespace daw::audio {
class TrackMeasurementPlugin;
}

/**
 * @brief Result of attempting to load a plugin
 */
struct PluginLoadResult {
    bool success = false;
    juce::String errorMessage;
    te::Plugin::Ptr plugin;

    static PluginLoadResult Success(const te::Plugin::Ptr& p) {
        return {true, {}, p};
    }
    static PluginLoadResult Failure(const juce::String& msg) {
        return {false, msg, nullptr};
    }
};

/**
 * @brief Manages plugin/device synchronization and lifecycle
 *
 * Responsibilities:
 * - Plugin/device mapping (DeviceId ↔ TE Plugin)
 * - Device processor management
 * - Plugin synchronization from TrackManager
 * - Built-in and external plugin loading
 * - Device → Plugin conversion
 *
 * Thread Safety:
 * - All operations protected by internal pluginLock_
 * - Lookup methods are lock-protected
 *
 * Dependencies:
 * - te::Engine& (for plugin cache, known plugin list)
 * - te::Edit& (for plugin cache)
 * - TrackController& (for track lookup/creation)
 * - PluginWindowBridge& (for closing plugin windows)
 * - TransportStateManager& (for tone generator bypass state)
 */
class PluginManager : public daw::audio::DrumGridPlugin::Listener {
  public:
    /**
     * @brief Construct PluginManager with required dependencies
     * @param engine Reference to the Tracktion Engine instance
     * @param edit Reference to the current Edit (project)
     * @param trackController Reference to TrackController for track operations
     * @param pluginWindowBridge Reference to PluginWindowBridge for window management
     * @param transportState Reference to TransportStateManager for transport state
     */
    PluginManager(te::Engine& engine, te::Edit& edit, TrackController& trackController,
                  PluginWindowBridge& pluginWindowBridge, TransportStateManager& transportState);

    // =========================================================================
    // Plugin/Device Lookup
    // =========================================================================

    // Path-based lookup — the section is part of the key.
    te::Plugin::Ptr getPlugin(const ChainNodePath& devicePath) const;

    DeviceProcessor* getDeviceProcessor(const ChainNodePath& devicePath) const;

    /**
     * @brief Reverse lookup: get DeviceId for a TE Plugin pointer
     * @param plugin The TE plugin pointer
     * @return The DeviceId, or INVALID_DEVICE_ID if not found
     */
    DeviceId getDeviceIdForPlugin(te::Plugin* plugin) const;
    ChainNodePath getDevicePathForPlugin(te::Plugin* plugin) const;

    // =========================================================================
    // Plugin Synchronization
    // =========================================================================

    /**
     * @brief Sync all tracks' plugins using diff-based global cleanup.
     *
     * Collects all valid device/rack IDs across ALL tracks from TrackManager,
     * diffs against syncedDevices_ and syncedRacks_, removes global orphans,
     * then runs per-track additive sync for each track.
     *
     * Use this when multiple tracks may have changed (e.g. tracksChanged,
     * project load). For single-track device changes, use syncTrackPlugins().
     */
    void syncAllPlugins();

    /**
     * @brief Sync a single track's plugins from TrackManager to Tracktion Engine
     * @param trackId The MAGDA track ID
     *
     * - Removes TE plugins owned by this track that no longer exist in TrackManager
     * - Adds new plugins for MAGDA devices without TE counterparts
     * - Ensures VolumeAndPan and LevelMeter are positioned correctly
     *
     * Safe for single-track calls: uses stored trackId for ownership scoping,
     * never touches devices on other tracks.
     */
    void syncTrackPlugins(TrackId trackId);

    /**
     * @brief Prepare the live TE graph for an explicit chain-element move.
     *
     * Captures live plugin state, then detaches runtime mappings that cannot be
     * carried across the requested source/destination containers. This is used
     * by TrackManager::moveChainElement, including undo/redo commands, so scope
     * transitions don't leave a stale plugin in the old chain.
     */
    void prepareForChainElementMove(const ChainNodePath& sourceElementPath,
                                    const ChainNodePath& destinationChainPath);

    /**
     * @brief Clean up all PluginManager state for a deleted track
     * @param trackId The MAGDA track ID being removed
     *
     * Purges syncedDevices_ entries for the deleted track, along with the
     * pluginToDevice_ reverse index, sidechainMonitors_, and racks.
     * Also cleans up cross-track sidechain references.
     */
    void cleanupTrackPlugins(TrackId trackId);

    // =========================================================================
    // Plugin Loading
    // =========================================================================

    /**
     * @brief Load a built-in Tracktion plugin
     * @param trackId The MAGDA track ID
     * @param type Plugin type (e.g., "tone", "delay", "reverb", "eq", "compressor")
     * @return The loaded plugin, or nullptr on failure
     */
    te::Plugin::Ptr loadBuiltInPlugin(TrackId trackId, const juce::String& type);

    /**
     * @brief Load an external plugin (VST3, AU)
     * @param trackId The MAGDA track ID
     * @param description Plugin description from plugin scan
     * @return PluginLoadResult with success status, error message, and plugin pointer
     */
    PluginLoadResult loadExternalPlugin(TrackId trackId, const juce::PluginDescription& description,
                                        int insertIndex = -1);

    /**
     * @brief Add a level meter plugin to a track for metering
     * @param trackId The MAGDA track ID
     * @return The level meter plugin
     */
    te::Plugin::Ptr addLevelMeterToTrack(TrackId trackId);

    /**
     * @brief Ensure VolumeAndPanPlugin is at the correct position (near end of chain)
     * @param track The Tracktion Engine audio track
     */
    void ensureVolumePluginPosition(te::AudioTrack* track) const;

    /**
     * @brief Callback invoked when a plugin fails to load
     * Parameters: deviceId, error message
     */
    std::function<void(DeviceId, const juce::String&)> onPluginLoadFailed;

    /**
     * @brief Callback invoked when an async plugin load completes (success or failure)
     * Used to trigger UI refresh after deferred external plugin loading.
     * Parameters: trackId
     */
    std::function<void(TrackId)> onAsyncPluginLoaded;

    /**
     * @brief Callback invoked when a plugin-order-only change requires a TE
     * playback graph restart. Primarily useful for diagnostics/tests because
     * TE does not restart automatically for ValueTree child-order changes.
     */
    std::function<void(TrackId, const juce::String&)> onPluginOrderGraphRestartRequested;

    // =========================================================================
    // Rack Plugin Creation
    // =========================================================================

    /**
     * @brief Create a plugin from DeviceInfo without inserting it onto a track
     *
     * Same creation logic as loadDeviceAsPlugin() but returns the plugin without
     * inserting it into a track's plugin list. Used for loading plugins inside racks.
     *
     * @param trackId The MAGDA track ID (for context, e.g. finding the track for instrument
     * wrapping)
     * @param device The device info describing the plugin to create
     * @return The created plugin, or nullptr on failure
     */
    te::Plugin::Ptr createPluginOnly(TrackId trackId, const DeviceInfo& device);

    /**
     * @brief Register a DeviceProcessor for a plugin inside a rack
     *
     * Creates an ExternalPluginProcessor (for external plugins), calls
     * populateParameters(), and stores it in syncedDevices_ so parameter
     * enumeration works the same as for standalone plugins.
     *
     * @param deviceId The MAGDA device ID inside the rack
     * @param plugin The TE plugin created for this device
     */
    void registerRackPluginProcessor(const ChainNodePath& devicePath, te::Plugin::Ptr plugin,
                                     const DeviceInfo& device);

    /**
     * @brief Refresh DeviceInfo.parameters for a device whose processor's
     *        parameter set changed at runtime.
     *
     * Re-runs `populateParameters` on the device's processor and pushes
     * the result into TrackManager. Used by plugins that swap their
     * parameter layout post-construction (FaustPlugin reloading a
     * .dsp). No-op if no processor is registered for the device.
     */
    void refreshDeviceParameters(const ChainNodePath& devicePath);

    /**
     * @brief Sync a multi-output track's plugin chain
     * Creates/manages the RackInstance for the output pair on the TE track.
     * @param trackId The multi-out track ID
     * @param trackInfo The TrackInfo for the multi-out track
     */
    void syncMultiOutTrack(TrackId trackId, const TrackInfo& trackInfo);

    /**
     * @brief Sync master channel plugins to TE's master plugin list
     */
    void syncMasterPlugins();

    /**
     * @brief Get the InstrumentRackManager for multi-output access
     */
    InstrumentRackManager& getInstrumentRackManager() {
        return instrumentRackManager_;
    }

    /**
     * @brief Get the RackSyncManager for rack audio routing
     */
    RackSyncManager& getRackSyncManager() {
        return rackSyncManager_;
    }
    const RackSyncManager& getRackSyncManager() const {
        return rackSyncManager_;
    }

    // =========================================================================
    // Plugin State Capture/Restore
    // =========================================================================

    /**
     * @brief Capture native state from all loaded external plugins into DeviceInfo
     *
     * Iterates syncedDevices_, calls flushPluginStateToValueTree() on each
     * ExternalPlugin, reads the base64 state property, and writes it to the
     * corresponding DeviceInfo::pluginState in TrackManager.
     */
    void captureAllPluginStates();

    /**
     * @brief Capture a single device's plugin state into its DeviceInfo.
     * Call before removing a device to preserve its state for undo.
     */
    void capturePluginState(const ChainNodePath& devicePath);

    /**
     * @brief Restore plugin native state from DeviceInfo onto a TE plugin
     *
     * Reads DeviceInfo::pluginState and, if non-empty, sets it on the TE
     * plugin's ValueTree state property so the plugin reads it during init.
     *
     * @param devicePath The device whose state to restore
     * @param plugin The TE plugin to apply state to
     */
    void restorePluginState(const ChainNodePath& devicePath, te::Plugin::Ptr plugin);

    // =========================================================================
    // Utilities
    // =========================================================================

    /**
     * @brief Purge stale map entries that reference tracks/devices no longer in TrackManager
     *
     * Safety net called at the end of syncAll() to catch any entries that
     * slipped through per-track cleanup. Validates all map entries against
     * current TrackManager state and removes orphans.
     */
    void purgeStaleEntries();

    /**
     * @brief Validate internal map consistency (debug builds only)
     *
     * Checks that all map entries reference valid tracks and devices.
     * Logs warnings on inconsistency. No-op in release builds.
     */
    void validateMappingConsistency();

    /**
     * @brief Clear all plugin mappings and processors
     * Called during shutdown to clean up state
     */
    void clearAllMappings();

    /**
     * @brief Update transport-synced processors (e.g., tone generators)
     * @param isPlaying Current transport playing state
     *
     * Called by AudioBridge when transport state changes to update
     * processors that need to sync with transport (bypass when stopped)
     */
    void updateTransportSyncedProcessors(bool isPlaying);

    /**
     * @brief Resync only device-level modifiers (LFO, Random, etc.) for a track
     *
     * Lighter than full syncTrackPlugins — only rebuilds TE modifier assignments.
     * Used when modifier properties change (rate, waveform, sync) without structural changes.
     */
    void resyncDeviceModifiers(TrackId trackId);

    /**
     * @brief Update rack/chain volume, pan, and mute/solo without structural resync
     *
     * Lighter than syncTrackPlugins — only pushes property values to existing TE plugins.
     */
    void syncRackProperties(TrackId trackId);

    /**
     * @brief Trigger note-on resync on all TE LFO modifiers for a track
     *
     * Thread-safe: can be called from MIDI thread. The actual resync happens
     * on the next audio block. Only affects LFOs with syncType == note.
     */
    void triggerLFONoteOn(TrackId trackId);

    /**
     * @brief Trigger note-on on cached sidechain LFO modifiers for a source track
     *
     * Thread-safe: can be called from audio or MIDI thread.
     * Uses a pre-computed cache of LFO modifier pointers, so no TrackManager
     * scan is needed. Handles both self-track and cross-track LFO triggering.
     *
     * @param sourceTrackId The sidechain source track
     * @param modeFilter If set, only trigger LFOs whose MAGDA mod uses this trigger mode.
     *                   Pass std::nullopt to trigger all (legacy behavior).
     */
    void triggerSidechainNoteOn(TrackId sourceTrackId,
                                std::optional<LFOTriggerMode> modeFilter = std::nullopt);

    /**
     * @brief Gate (zero) all cached LFO values for a track when no notes are held.
     *
     * Called from SidechainMonitorPlugin on the audio/render thread when
     * localHeldNoteCount_ reaches 0. Sets currentValue = 0 on note-triggered
     * LFOs so the modulation stops, matching the message-thread assignment
     * gating behavior during live playback.
     */
    void gateSidechainLFOs(TrackId sourceTrackId);

    /**
     * @brief Rebuild the sidechain LFO cache for all tracks
     *
     * Must be called on the message thread after sidechain config, modifier,
     * or track structure changes. Collects te::LFOModifier* pointers from
     * syncedDevices_ modifiers and RackSyncManager into a flat per-track array.
     */
    void rebuildSidechainLFOCache();

    /**
     * @brief Copy each TE LFO's current phase + value into its MAGDA ModInfo.
     *
     * Single source of truth fix: instead of running an independent visual
     * LFO simulation in ModulatorEngine that drifts against TE's audio LFO
     * (different start times, no phase sync), overlay TE's atomic-loaded
     * `getCurrentPhase()` / `getCurrentValue()` onto every ModInfo so the
     * UI marker tracks the audio exactly. Called from ModulatorEngine's
     * post-update hook after the local sim's trigger/running tracking has
     * run. Walks track-level mods, top-level device mods, instrument-rack
     * inner mods, and rack-internal mods (delegates to RackSyncManager
     * for the last). Safe to call from the message thread.
     */
    void syncLFOValuesToVisuals();

    /**
     * @brief Prepare LFO assignments for offline rendering.
     *
     * Sets all MIDI/Audio-triggered LFO assignment values to their link.amount
     * (enabling modulation) and prevents the message-thread timer from zeroing
     * them during the render. Call restoreAfterRendering() when done.
     */
    void prepareForRendering();

    /**
     * @brief Restore normal LFO gating after offline rendering.
     */
    void restoreAfterRendering();

    /// True between prepareForRendering() and restoreAfterRendering() -- i.e.
    /// while an offline render (export / mix analysis) is in progress.
    bool isRenderingActive() const {
        return renderingActive_.load(std::memory_order_acquire);
    }

    /**
     * @brief Reset sidechain monitor held-note counts and re-gate triggered LFOs.
     *
     * Call when transport stops so that held-note counts don't leak into the next
     * playback session (which would prevent gating from firing).
     */
    void resetSidechainState();

    /**
     * @brief Route a macro value change to the appropriate TE infrastructure
     * @param trackId The track containing the macro
     * @param scope ChainScope::Track / ::Rack / ::Device — disambiguates ownerId
     * @param ownerId TrackId / RackId / DeviceId, picked by `scope`
     * @param macroIndex Index into the macro array
     * @param value Normalized value (0.0 to 1.0)
     */
    void setMacroValue(TrackId trackId, ChainScope scope, int ownerId, int macroIndex, float value);

    /**
     * @brief Resolve an AutomationTarget::Macro to its TE MacroParameter for
     * automation playback. devicePath identifies device or rack scope; pass an
     * invalid devicePath for a track-level macro.
     */
    te::AutomatableParameter* findMacroParameterForAutomation(TrackId trackId,
                                                              const ChainNodePath& devicePath,
                                                              int macroIndex) const;

    /**
     * @brief Resolve an AutomationTarget::ModParameter to a TE Modifier's
     * AutomatableParameter. modParamIndex maps into the modifier's
     * getAutomatableParameters() list (0 = rate for LFOModifier). modId is a
     * per-array slot index, NOT globally unique — pass devicePath to identify
     * the owning scope (rack, top-level device, or track).
     */
    te::AutomatableParameter* findModifierParameterForAutomation(TrackId trackId,
                                                                 const ChainNodePath& devicePath,
                                                                 ModId modId,
                                                                 int modParamIndex) const;

    /**
     * @brief Sync sidechain routing for devices on a track
     * For each device with an active sidechain config, sets the TE plugin's
     * sidechain source to the corresponding TE track.
     * Called from syncTrackPlugins after devices are created.
     */
    void syncSidechains(TrackId trackId, te::AudioTrack* teTrack);

    /**
     * @brief Check if a track needs a SidechainMonitorPlugin and insert/remove accordingly.
     * Call when trigger mode or sidechain source changes.
     * @param trackId The track to check
     */
    void checkSidechainMonitor(TrackId trackId);

    /**
     * @brief Ensure a SidechainMonitorPlugin exists on a source track
     * Inserts the monitor at position 0 if not already present.
     * @param sourceTrackId The track that is a MIDI sidechain source
     */
    void ensureSidechainMonitor(TrackId sourceTrackId);

    /**
     * @brief Remove the SidechainMonitorPlugin from a source track
     * Called when no devices reference this track as a MIDI sidechain source.
     * @param sourceTrackId The track to remove the monitor from
     */
    void removeSidechainMonitor(TrackId sourceTrackId);

    /**
     * @brief Check if a track needs an AudioSidechainMonitorPlugin and insert/remove.
     * @param trackId The track to check
     */
    void checkAudioSidechainMonitor(TrackId trackId);
    void ensureAudioSidechainMonitor(TrackId sourceTrackId);
    void removeAudioSidechainMonitor(TrackId sourceTrackId);

    /**
     * @brief Ensure the always-on per-track measurement tap exists (issue #1388).
     *
     * Inserts a TrackMeasurementPlugin post-fader (appended at the end of the
     * chain, after volume/pan) on the given track, or on the master when
     * trackId == MASTER_TRACK_ID. The master tap is created with true-peak
     * enabled; per-track taps use sample peak only. Idempotent: returns the
     * existing tap if already present. Returns nullptr if the track has no TE
     * audio track. The tap is dormant until a consumer enables it.
     */
    daw::audio::TrackMeasurementPlugin* ensureTrackMeasurementTap(TrackId trackId);
    void removeTrackMeasurementTap(TrackId trackId);
    daw::audio::TrackMeasurementPlugin* getTrackMeasurementTap(TrackId trackId) const;

    /**
     * @brief Ensure a MidiReceivePlugin exists before a target device for MIDI sidechain.
     * @param trackId The destination track
     * @param deviceId The device that needs MIDI injection
     * @param sourceTrackId The source track providing MIDI
     */
    void ensureMidiReceive(const ChainNodePath& devicePath, TrackId sourceTrackId);

    /**
     * @brief Remove a MidiReceivePlugin for a device when its MIDI sidechain is cleared.
     * @param trackId The destination track
     * @param deviceId The device that no longer needs MIDI injection
     */
    void removeMidiReceive(const ChainNodePath& devicePath);
    void requestPluginOrderGraphRestart(TrackId trackId, const juce::String& reason);

    /**
     * @brief Set a device macro parameter value on the TE MacroParameter
     */
    void setMacroValue(const ChainNodePath& devicePath, int macroIndex, float value);

    /**
     * @brief Sync multi-out tracks for a DrumGrid device
     * Activates/deactivates multi-out pairs to match current non-empty chains.
     */
    void syncDrumGridMultiOutTracks(const ChainNodePath& drumGridPath,
                                    daw::audio::DrumGridPlugin* drumGrid);

    /**
     * @brief Register per-pad chain plugins in syncedDevices_ for macro/mod linking
     */
    void syncDrumGridPadPlugins(const ChainNodePath& drumGridPath,
                                daw::audio::DrumGridPlugin* drumGrid);

    // DrumGridPlugin::Listener
    void drumGridChainsChanged(daw::audio::DrumGridPlugin* plugin) override;

    // Resolve a ChainNodePath to a te::Plugin* for ModifierSync's TargetPluginLookup.
    // Track-bound plugin first; falls back to instrument-rack inner plugin.
    te::Plugin* lookupTargetPluginForModifier(const ChainNodePath& devicePath) const;

  private:
    // Internal device → plugin conversion (used by syncTrackPlugins)
    te::Plugin::Ptr loadDeviceAsPlugin(const ChainNodePath& devicePath, const DeviceInfo& device,
                                       int insertIndex = -1);

    void detachDeviceRuntimeForChainMove(const ChainNodePath& devicePath);
    void detachRackRuntimeForChainMove(RackId rackId);
    void removeDrumGridPadDevicesLocked(const ChainNodePath& drumGridPath);

    // Poll for async plugin load completion (TE's background thread instantiation)
    void pollAsyncPluginLoad(const ChainNodePath& devicePath, te::Plugin::Ptr plugin);

    // Create a TE internal plugin, restoring saved ValueTree state if available.
    // Falls back to creating a fresh plugin from xmlTypeName when no saved state exists.
    te::Plugin::Ptr createInternalPlugin(const juce::String& xmlTypeName,
                                         const juce::String& savedPluginState);

    // References to dependencies (not owned)
    te::Engine& engine_;
    te::Edit& edit_;
    TrackController& trackController_;
    PluginWindowBridge& pluginWindowBridge_;
    TransportStateManager& transportState_;

    // Instrument rack wrapping (synth + audio passthrough)
    InstrumentRackManager instrumentRackManager_;

    // Rack audio routing (MAGDA RackInfo → TE RackType)
    RackSyncManager rackSyncManager_;

    // Device-level modifier sync (for standalone devices, not inside MAGDA racks)
    void syncDeviceModifiers(TrackId trackId, te::AudioTrack* teTrack);

    // Update existing modifier properties in-place (rate, waveform, sync, phase)
    // without destroying/recreating modifiers. Used for non-structural changes.
    void updateDeviceModifierProperties(TrackId trackId);

    // Compute (activeModCount, totalLinkCount) fingerprint across device+track mods.
    std::pair<int, int> computeModLinkFingerprint(TrackId trackId,
                                                  const TrackInfo* trackInfo) const;

    // Check whether a track needs a SidechainMonitorPlugin (MIDI sidechain source).
    bool trackNeedsSidechainMonitor(TrackId trackId) const;

    // Check whether a track needs an AudioSidechainMonitorPlugin (audio sidechain source).
    bool trackNeedsAudioSidechainMonitor(TrackId trackId) const;

    // Per-device consolidated state. All device-scoped data lives here,
    // keyed by ChainNodePath so devices in different chain sections can share
    // the same DeviceId without colliding.
    //
    // Step 2 of issue #1131: `modifiers` switched from a vector to a map keyed
    // by ModId so it matches RackSyncManager's `innerModifiers` shape. The
    // unified ModifierSyncWalker reads/writes both call sites through the
    // same `ModifierSyncState` view.
    struct SyncedDevice {
        TrackId trackId = INVALID_TRACK_ID;            // MAGDA track that owns this device
        te::Plugin::Ptr plugin;                        // Always set on creation
        std::unique_ptr<DeviceProcessor> processor;    // nullptr until async load completes
        std::map<ModId, te::Modifier::Ptr> modifiers;  // Can be empty
        std::map<ModId, std::unique_ptr<CurveSnapshotHolder>>
            curveSnapshots;                              // ModId-only (device scope implicit)
        std::map<int, te::MacroParameter*> macroParams;  // Can be empty
        te::Plugin::Ptr midiReceivePlugin;               // Can be null
        te::Plugin::Ptr midiRestorePlugin;               // Can be null
        bool isPendingLoad = false;                      // In-flight async load
    };
    using SyncedDeviceMap = std::map<ChainNodePath, SyncedDevice>;

    SyncedDeviceMap::iterator findSyncedDevice(const ChainNodePath& devicePath);
    SyncedDeviceMap::const_iterator findSyncedDevice(const ChainNodePath& devicePath) const;

    SyncedDeviceMap syncedDevices_;

    // Reverse index: TE Plugin* → ChainNodePath (maintained alongside .plugin assignments)
    std::map<te::Plugin*, ChainNodePath> pluginToDevice_;

    // Track-level mod state (for track-scope modulators that target any device)
    struct TrackModState {
        std::map<ModId, te::Modifier::Ptr> modifiers;
        std::map<ModId, std::unique_ptr<CurveSnapshotHolder>> curveSnapshots;
    };
    std::map<TrackId, TrackModState> trackModStates_;

    // Track-level macro TE state (macroIndex → TE MacroParameter)
    std::map<TrackId, std::map<int, te::MacroParameter*>> trackMacroParams_;

    // Structural fingerprint for mod sync: {activeModCount, totalLinkCount}
    // Used to decide if resyncDeviceModifiers needs a full rebuild or just property update.
    std::map<TrackId, std::pair<int, int>> modLinkFingerprints_;

    // DrumGrid ChainNodePath -> pad-plugin ChainNodePaths registered in syncedDevices_
    std::map<ChainNodePath, std::set<ChainNodePath>> drumGridPadDevices_;

    // Sidechain monitor plugins (sourceTrackId → SidechainMonitorPlugin)
    std::map<TrackId, te::Plugin::Ptr> sidechainMonitors_;

    // Audio sidechain monitor plugins (sourceTrackId → AudioSidechainMonitorPlugin)
    std::map<TrackId, te::Plugin::Ptr> audioSidechainMonitors_;

    // Per-track measurement taps (trackId → TrackMeasurementPlugin), issue #1388.
    // MASTER_TRACK_ID keys the master tap. Lazily inserted on first enable.
    std::map<TrackId, te::Plugin::Ptr> trackMeasurementTaps_;

    // Pre-computed sidechain LFO cache: indexed by source TrackId.
    // Double-buffered: message thread writes to inactive buffer then atomically
    // swaps the pointer. Audio thread reads through the atomic pointer with no lock.
    struct PerTrackEntry {
        static constexpr int kMaxLFOs = 64;
        std::array<te::LFOModifier*, kMaxLFOs> lfos{};
        std::array<bool, kMaxLFOs> isCrossTrack{};  // true = sidechain destination on another track
        std::array<LFOTriggerMode, kMaxLFOs> trigMode{};  // trigger mode of corresponding MAGDA mod
        int count = 0;
    };
    static constexpr int kMaxCacheTracks = 512;
    struct SidechainCache {
        std::array<PerTrackEntry, kMaxCacheTracks> entries{};
    };
    SidechainCache cacheBuffers_[2];
    std::atomic<SidechainCache*> activeCache_{&cacheBuffers_[0]};
    int writeCacheIndex_ =
        1;  // message thread only — starts at 1 so first write goes to inactive buffer

    // When true, skip gating during offline render and block full device resync.
    std::atomic<bool> renderingActive_{false};

    // Deferred CurveSnapshotHolder deletion to prevent audio-thread use-after-free.
    // Holders are moved here after clearing LFO callbacks, then drained at the
    // start of the next sync operation (by which time the audio thread has moved on).
    std::vector<std::unique_ptr<CurveSnapshotHolder>> deferredHolders_;

    // Thread safety
    mutable juce::CriticalSection pluginLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginManager)
    JUCE_DECLARE_WEAK_REFERENCEABLE(PluginManager)
};

}  // namespace magda
