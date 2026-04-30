#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "ChainNode.hpp"
#include "SelectionManager.hpp"
#include "TrackInfo.hpp"
#include "TrackTypes.hpp"
#include "ViewModeState.hpp"

namespace magda {

// Forward declarations
class AudioEngine;

/**
 * @brief Master channel state
 */
struct MasterChannelState {
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool soloed = false;
    TrackViewSettingsMap viewSettings;  // Visibility per view mode

    bool isVisibleIn(ViewMode mode) const {
        return viewSettings.isVisible(mode);
    }
};

/**
 * @brief Listener interface for track changes
 */
class TrackManagerListener {
  public:
    virtual ~TrackManagerListener() = default;

    // Called when tracks are added, removed, or reordered
    virtual void tracksChanged() = 0;

    // Called when a specific track's properties change
    virtual void trackPropertyChanged(int trackId) {
        juce::ignoreUnused(trackId);
    }

    // Called when master channel properties change
    virtual void masterChannelChanged() {}

    // Called when track selection changes
    virtual void trackSelectionChanged(TrackId trackId) {
        juce::ignoreUnused(trackId);
    }

    // Called when devices on a track change (added, removed, reordered, bypassed)
    virtual void trackDevicesChanged(TrackId trackId) {
        juce::ignoreUnused(trackId);
    }

    // Called when modifier properties change (rate, waveform, sync, etc.) but not structure
    virtual void deviceModifiersChanged(TrackId trackId) {
        juce::ignoreUnused(trackId);
    }

    // Called when an audio-triggered mod fires (gate opens) — sourceTrackId is the sidechain source
    virtual void audioSidechainTriggered(TrackId sourceTrackId) {
        juce::ignoreUnused(sourceTrackId);
    }

    // Called when a device parameter changes (gain, level, etc.)
    virtual void devicePropertyChanged(DeviceId deviceId) {
        juce::ignoreUnused(deviceId);
    }

    // Called when a device parameter value changes (for live parameter updates)
    virtual void deviceParameterChanged(DeviceId deviceId, int paramIndex, float newValue) {
        juce::ignoreUnused(deviceId, paramIndex, newValue);
    }

    // Called when a macro knob value changes (for audio engine sync).
    // `scope` tells the receiver how to interpret `ownerId`:
    //   ChainScope::Track  → ownerId is the TrackId
    //   ChainScope::Rack   → ownerId is the RackId
    //   ChainScope::Device → ownerId is the DeviceId
    // Track and Device id namespaces overlap in practice, so `scope` is the
    // only way to disambiguate — do not key off `ownerId == trackId`.
    virtual void macroValueChanged(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                                   float value) {
        juce::ignoreUnused(trackId, scope, ownerId, macroIndex, value);
    }

    // Called when a single modifier parameter (rate, depth, etc.) is edited
    // by the user. Distinct from deviceModifiersChanged() which only carries
    // a TrackId and is used by the audio engine to resync the whole modifier
    // list. This carries enough scope info to address the exact parameter
    // for automation recording.
    virtual void modParameterChanged(TrackId trackId, const ChainNodePath& devicePath, ModId modId,
                                     int paramIndex, float value) {
        juce::ignoreUnused(trackId, devicePath, modId, paramIndex, value);
    }
};

/**
 * @brief Singleton manager for all tracks in the project
 *
 * Provides CRUD operations for tracks and notifies listeners of changes.
 */
class TrackManager {
  public:
    static constexpr int MAX_SENDS_PER_TRACK = 8;  // Tracktion Engine aux bus limit

    static TrackManager& getInstance();

    // Prevent copying
    TrackManager(const TrackManager&) = delete;
    TrackManager& operator=(const TrackManager&) = delete;

    // Allocate a unique DeviceId (used by DrumGrid for per-pad chain plugins)
    DeviceId allocateDeviceId() {
        return nextDeviceId_++;
    }
    // Ensure the counter is above a restored ID (prevents collisions on project load)
    void ensureDeviceIdAbove(DeviceId id) {
        nextDeviceId_ = std::max(nextDeviceId_, id + 1);
    }

    /**
     * @brief Set the audio engine reference for routing operations
     * Should be called once during app initialization
     */
    void setAudioEngine(AudioEngine* audioEngine);

    /**
     * @brief Shutdown and clear all resources
     * Call during app shutdown to prevent static cleanup issues
     */
    void shutdown() {
        tracks_.clear();  // Clear JUCE::String objects before JUCE cleanup
        listeners_.clear();
        audioEngine_ = nullptr;
    }

    /**
     * @brief Get the audio engine reference
     * @return Pointer to audio engine, or nullptr if not set
     */
    AudioEngine* getAudioEngine() const {
        return audioEngine_;
    }

    /**
     * @brief Extract DeviceInfo from a dropped plugin DynamicObject.
     *
     * Parses the standard plugin properties (name, manufacturer, uniqueId,
     * format, isInstrument, fileOrIdentifier) into a DeviceInfo struct.
     */
    static DeviceInfo deviceInfoFromPluginObject(const juce::DynamicObject& pluginObj);

    /**
     * @brief Create a new track from a dropped plugin DynamicObject.
     *
     * Extracts DeviceInfo, creates an Instrument or Audio track named after the
     * plugin, adds the device, and selects the new track.
     *
     * @return The new track ID, or INVALID_TRACK_ID on failure.
     */
    static TrackId createTrackWithPlugin(const juce::DynamicObject& pluginObj);

    // Track operations
    TrackId createTrack(const juce::String& name = "", TrackType type = TrackType::Audio);
    TrackId createGroupTrack(const juce::String& name = "");
    void deleteTrack(TrackId trackId);
    /**
     * Duplicate a track. When `includeDevices` is false, the new track is
     * created empty (no plugins / racks / chain elements), giving a clean
     * slate for workflows that want content-only duplication.
     */
    TrackId duplicateTrack(TrackId trackId, bool includeDevices = true);
    void restoreTrack(const TrackInfo& trackInfo);  // Used by undo system
    void moveTrack(TrackId trackId, int newIndex);

    // Hierarchy operations
    void addTrackToGroup(TrackId trackId, TrackId groupId);
    void removeTrackFromGroup(TrackId trackId);
    TrackId createTrackInGroup(TrackId groupId, const juce::String& name = "",
                               TrackType type = TrackType::Audio);
    std::vector<TrackId> getChildTracks(TrackId groupId) const;
    std::vector<TrackId> getTopLevelTracks() const;
    std::vector<TrackId> getAllDescendants(TrackId trackId) const;

    /**
     * @brief Preview a MIDI note on a track (for keyboard audition)
     * @param trackId Track to send note to
     * @param noteNumber MIDI note number (0-127)
     * @param velocity Velocity (0-127)
     * @param isNoteOn True for note-on, false for note-off
     */
    void previewNote(TrackId trackId, int noteNumber, int velocity, bool isNoteOn);

    // Multi-output management
    TrackId activateMultiOutPair(TrackId parentTrackId, DeviceId deviceId, int pairIndex);
    void deactivateMultiOutPair(TrackId parentTrackId, DeviceId deviceId, int pairIndex);
    void deactivateAllMultiOutPairs(TrackId parentTrackId, DeviceId deviceId);

    // Access
    const std::vector<TrackInfo>& getTracks() const {
        return tracks_;
    }
    TrackInfo* getTrack(TrackId trackId);
    const TrackInfo* getTrack(TrackId trackId) const;
    int getTrackIndex(TrackId trackId) const;
    int getNumTracks() const {
        return static_cast<int>(tracks_.size());
    }

    // Track property setters (notify listeners)
    void setTrackName(TrackId trackId, const juce::String& name);
    void setTrackColour(TrackId trackId, juce::Colour colour);
    void setTrackVolume(TrackId trackId, float volume, bool fromAutomation = false);
    void setTrackPan(TrackId trackId, float pan, bool fromAutomation = false);
    void setTrackMuted(TrackId trackId, bool muted);
    void setTrackSoloed(TrackId trackId, bool soloed);
    void setTrackRecordArmed(TrackId trackId, bool armed);
    void setTrackInputMonitor(TrackId trackId, InputMonitorMode mode);
    void setTrackFrozen(TrackId trackId, bool frozen);
    void setTrackPlaybackMode(TrackId trackId, TrackPlaybackMode mode);
    void setAllTracksPlaybackMode(TrackPlaybackMode mode);
    bool isAnyTrackInSessionMode() const;
    void setTrackType(TrackId trackId, TrackType type);

    // Track routing setters (notify listeners and forward to bridges)
    void setTrackMidiInput(TrackId trackId, const juce::String& deviceId);
    void setTrackMidiOutput(TrackId trackId, const juce::String& deviceId);
    void setTrackAudioInput(TrackId trackId, const juce::String& deviceId);
    void setTrackAudioOutput(TrackId trackId, const juce::String& routing);

    // Send management (track → any track)
    void addSend(TrackId sourceTrackId, TrackId destTrackId);
    void removeSend(TrackId sourceTrackId, int busIndex);
    void setSendLevel(TrackId sourceTrackId, int busIndex, float level,
                      bool fromAutomation = false);

    // View settings
    void setTrackVisible(TrackId trackId, ViewMode mode, bool visible);
    void setTrackLocked(TrackId trackId, ViewMode mode, bool locked);
    void setTrackCollapsed(TrackId trackId, bool collapsed);
    void setTrackHeight(TrackId trackId, ViewMode mode, int height);

    // Signal chain management (unified list of devices and racks)
    const std::vector<ChainElement>& getChainElements(TrackId trackId) const;
    void moveNode(TrackId trackId, int fromIndex, int toIndex);

    // Device management on track
    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device);
    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device, int insertIndex);
    void removeDeviceFromTrack(TrackId trackId, DeviceId deviceId);
    void setDeviceBypassed(TrackId trackId, DeviceId deviceId, bool bypassed);
    void setChainBypassed(TrackId trackId, bool bypassed);
    DeviceInfo* getDevice(TrackId trackId, DeviceId deviceId);

    // Wrap a device in a new rack (device moves into the rack's first chain)
    RackId wrapDeviceInRack(TrackId trackId, DeviceId deviceId,
                            const juce::String& rackName = "Rack");
    RackId wrapDeviceInRackByPath(const ChainNodePath& devicePath,
                                  const juce::String& rackName = "Rack");

    // Rack management on track
    RackId addRackToTrack(TrackId trackId, const juce::String& name = "Rack");
    void removeRackFromTrack(TrackId trackId, RackId rackId);
    RackInfo* getRack(TrackId trackId, RackId rackId);
    const RackInfo* getRack(TrackId trackId, RackId rackId) const;
    void setRackBypassed(TrackId trackId, RackId rackId, bool bypassed);
    void setRackExpanded(TrackId trackId, RackId rackId, bool expanded);

    // Path-based rack lookup (works for nested racks at any depth)
    RackInfo* getRackByPath(const ChainNodePath& rackPath);
    const RackInfo* getRackByPath(const ChainNodePath& rackPath) const;

    // Chain management (within racks) - works for nested racks via path
    ChainId addChainToRack(const ChainNodePath& rackPath, const juce::String& name = "Chain");
    void removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId);
    void removeChainByPath(const ChainNodePath& chainPath);  // Path-based removal for nested chains
    ChainInfo* getChain(TrackId trackId, RackId rackId, ChainId chainId);
    const ChainInfo* getChain(TrackId trackId, RackId rackId, ChainId chainId) const;
    void setChainOutput(TrackId trackId, RackId rackId, ChainId chainId, int outputIndex);
    void setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted);
    void setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId, bool bypassed);
    void setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo);
    void setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volume);
    void setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan);
    void setChainExpanded(TrackId trackId, RackId rackId, ChainId chainId, bool expanded);
    void setRackVolume(TrackId trackId, RackId rackId, float volume);
    void setRackVolume(const ChainNodePath& rackPath, float volume);

    // Device management within chains
    DeviceId addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                              const DeviceInfo& device);
    DeviceId addDeviceToChainByPath(const ChainNodePath& chainPath, const DeviceInfo& device);
    DeviceId addDeviceToChainByPath(const ChainNodePath& chainPath, const DeviceInfo& device,
                                    int insertIndex);
    void removeDeviceFromChain(TrackId trackId, RackId rackId, ChainId chainId, DeviceId deviceId);
    void removeDeviceFromChainByPath(const ChainNodePath& devicePath);
    void moveDeviceInChain(TrackId trackId, RackId rackId, ChainId chainId, DeviceId deviceId,
                           int newIndex);
    void moveElementInChainByPath(const ChainNodePath& chainPath, int fromIndex, int toIndex);
    DeviceInfo* getDeviceInChain(TrackId trackId, RackId rackId, ChainId chainId,
                                 DeviceId deviceId);
    DeviceInfo* getDeviceInChainByPath(const ChainNodePath& devicePath);
    const DeviceInfo* getDeviceInChainByPath(const ChainNodePath& devicePath) const;
    void setDeviceInChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                  DeviceId deviceId, bool bypassed);
    void setDeviceInChainBypassedByPath(const ChainNodePath& devicePath, bool bypassed);

    // Sidechain configuration (device-level)
    void setSidechainSource(DeviceId targetDevice, TrackId sourceTrack, SidechainConfig::Type type);
    void clearSidechain(DeviceId targetDevice);

    // Sidechain configuration (rack-level)
    void setRackSidechainSource(const ChainNodePath& rackPath, TrackId sourceTrack,
                                SidechainConfig::Type type);
    void clearRackSidechain(const ChainNodePath& rackPath);

    // Device parameter setters (notify listeners for audio sync)
    void setDeviceGainDb(const ChainNodePath& devicePath, float gainDb);
    void setDeviceLevel(const ChainNodePath& devicePath, float level);  // 0-1 linear

    // Find the ChainNodePath for a device by its ID (searches all tracks recursively)
    ChainNodePath findDevicePath(DeviceId deviceId) const;

    // Update device parameters (called by AudioBridge when processor is created)
    void updateDeviceParameters(DeviceId deviceId, const std::vector<ParameterInfo>& params);
    void setDeviceVisibleParameters(DeviceId deviceId, const std::vector<int>& visibleParams);

    // Set a specific device parameter value
    void setDeviceParameterValue(const ChainNodePath& devicePath, int paramIndex, float value);

    /**
     * @brief Set a device parameter value from the plugin's native UI
     *
     * This method is called when the plugin's native UI changes a parameter.
     * Unlike setDeviceParameterValue(), this does NOT notify AudioBridge
     * to avoid feedback loops. It only updates the DeviceInfo and notifies
     * UI listeners for display updates.
     */
    void setDeviceParameterValueFromPlugin(const ChainNodePath& devicePath, int paramIndex,
                                           float value);

    /**
     * @brief Get plugin latency for a device by querying the audio engine
     * @param devicePath Path to the device in the chain
     * @return Latency in seconds, or 0 if not found
     */
    double getDeviceLatencySeconds(const ChainNodePath& devicePath);

    /**
     * @brief Get total plugin latency for a track (sum of all devices in chain)
     * @param trackId Track to query
     * @return Total latency in seconds, or 0 if not found
     */
    double getTrackLatencySeconds(TrackId trackId);

    // Nested rack management within chains
    RackId addRackToChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                          const juce::String& name = "Rack");
    RackId addRackToChainByPath(const ChainNodePath& chainPath, const juce::String& name = "Rack");
    void removeRackFromChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                             RackId nestedRackId);
    void removeRackFromChainByPath(const ChainNodePath& rackPath);

    // ========================================================================
    // Unified ChainNode-based modulation API (issue #1131 steps 1–3)
    // ========================================================================
    //
    // Every Track / Rack / Device macro & mod operation routes through one
    // path-driven implementation that resolves the path once and operates on
    // the resulting ChainNode view. Steps 1–2 introduced the unified ops and
    // the modifier-sync walker; step 3 deleted the scope-specific setTrack* /
    // setRack* / setDevice* triplet shims and rewrote every caller to use
    // these methods with a `ChainNodePath` (or `ChainNodePath::trackLevel`
    // for track-scope operations).
    //
    // resolveChainNode() walks a ChainNodePath through the track tree and
    // returns a non-owning view onto the macros / mods (and parameters, for
    // device nodes) at that path. Returns an invalid node (valid() == false)
    // if the path doesn't resolve.

    // Public form returns a const view — external callers should never bypass
    // the unified setters' notification logic. The mutable form is private and
    // used only by those setters; see private section below.
    ConstChainNode resolveChainNode(const ChainNodePath& path) const;

    // Unified macro management — works for Track, Rack, and Device scopes.
    void setMacroValue(const ChainNodePath& path, int macroIndex, float value);
    void setMacroTarget(const ChainNodePath& path, int macroIndex, MacroTarget target);
    void setMacroLinkAmount(const ChainNodePath& path, int macroIndex, MacroTarget target,
                            float amount);
    void setMacroLinkBipolar(const ChainNodePath& path, int macroIndex, MacroTarget target,
                             bool bipolar);
    void setMacroName(const ChainNodePath& path, int macroIndex, const juce::String& name);
    void removeMacroLink(const ChainNodePath& path, int macroIndex, MacroTarget target);
    void clearAllMacroLinks(const ChainNodePath& path, int macroIndex);
    void addMacroPage(const ChainNodePath& path);
    void removeMacroPage(const ChainNodePath& path);

    // Unified mod management — works for Track, Rack, and Device scopes.
    void addMod(const ChainNodePath& path, int slotIndex, ModType type,
                LFOWaveform waveform = LFOWaveform::Sine);
    void removeMod(const ChainNodePath& path, int modIndex);
    void setModAmount(const ChainNodePath& path, int modIndex, float amount);
    void setModTarget(const ChainNodePath& path, int modIndex, ModTarget target);
    void setModLinkAmount(const ChainNodePath& path, int modIndex, ModTarget target, float amount);
    void setModLinkBipolar(const ChainNodePath& path, int modIndex, ModTarget target, bool bipolar);
    void setModName(const ChainNodePath& path, int modIndex, const juce::String& name);
    void setModType(const ChainNodePath& path, int modIndex, ModType type);
    void setModWaveform(const ChainNodePath& path, int modIndex, LFOWaveform waveform);
    void setModRate(const ChainNodePath& path, int modIndex, float rate);
    void setModPhaseOffset(const ChainNodePath& path, int modIndex, float phaseOffset);
    void setModTempoSync(const ChainNodePath& path, int modIndex, bool tempoSync);
    void setModSyncDivision(const ChainNodePath& path, int modIndex, SyncDivision division);
    void setModTriggerMode(const ChainNodePath& path, int modIndex, LFOTriggerMode mode);
    void setModCurvePreset(const ChainNodePath& path, int modIndex, CurvePreset preset);
    void notifyModCurveChanged(const ChainNodePath& path);
    void setModAudioAttack(const ChainNodePath& path, int modIndex, float ms);
    void setModAudioRelease(const ChainNodePath& path, int modIndex, float ms);
    void removeModLink(const ChainNodePath& path, int modIndex, ModTarget target);
    void setModEnabled(const ChainNodePath& path, int modIndex, bool enabled);
    void addModPage(const ChainNodePath& path);
    void removeModPage(const ChainNodePath& path);

    // Modulation engine integration - updates LFO values silently (no UI notifications)
    // bpm parameter is used for tempo-synced LFOs (default 120 if not provided)
    // transportJustStarted/Looped flags trigger phase reset for Transport trigger mode
    void updateAllMods(double deltaTime, double bpm = 120.0, bool transportJustStarted = false,
                       bool transportJustLooped = false, bool transportJustStopped = false);

    /**
     * @brief Signal that a MIDI note-on was received on a track
     *
     * Called from MidiBridge when MIDI arrives. The next updateAllMods() tick
     * will reset phase on any mods with LFOTriggerMode::MIDI on this track.
     */
    void triggerMidiNoteOn(TrackId trackId);
    void triggerMidiNoteOff(TrackId trackId);

    /** Look up a ModInfo by its id across all devices and racks on a track. */
    const ModInfo* getModById(TrackId trackId, ModId modId) const;

    /**
     * @brief Update transport state for LFO trigger sync
     *
     * Called from TracktionEngineWrapper's timer callback with the current
     * transport state. The next updateAllMods() tick will use these values.
     */
    void updateTransportState(bool playing, double bpm, bool justStarted, bool justLooped);

    struct TransportSnapshot {
        double bpm;
        bool justStarted;
        bool justLooped;
        bool justStopped;
    };
    TransportSnapshot consumeTransportState();

    // ========================================================================
    // Path Resolution - Centralized tree traversal
    // ========================================================================

    /**
     * @brief Result of resolving a ChainNodePath
     *
     * Contains pointers to the actual data elements along the path,
     * plus a human-readable path string for display.
     */
    struct ResolvedPath {
        bool valid = false;
        juce::String displayPath;  // "Rack > Chain > Device" format

        // Pointers to actual elements (null if not applicable)
        const RackInfo* rack = nullptr;
        const ChainInfo* chain = nullptr;
        const DeviceInfo* device = nullptr;

        // For nested structures, these point to the final element
        // The path traversal history is in displayPath
    };

    /**
     * @brief Resolve a ChainNodePath to actual data elements
     *
     * Walks the recursive tree structure following the path steps,
     * returning pointers to the actual elements and a display string.
     */
    ResolvedPath resolvePath(const ChainNodePath& path) const;

    // Query tracks by view
    std::vector<TrackId> getVisibleTracks(ViewMode mode) const;
    std::vector<TrackId> getVisibleTopLevelTracks(ViewMode mode) const;

    // Track selection
    void setSelectedTrack(TrackId trackId);
    TrackId getSelectedTrack() const {
        return selectedTrackId_;
    }
    void setSelectedTracks(const std::unordered_set<TrackId>& trackIds);
    const std::unordered_set<TrackId>& getSelectedTracks() const {
        return selectedTrackIds_;
    }

    // Chain selection (for plugin browser context menu)
    void setSelectedChain(TrackId trackId, RackId rackId, ChainId chainId);
    void clearSelectedChain();
    bool hasSelectedChain() const {
        return selectedChainTrackId_ != INVALID_TRACK_ID &&
               selectedChainRackId_ != INVALID_RACK_ID && selectedChainId_ != INVALID_CHAIN_ID;
    }
    TrackId getSelectedChainTrackId() const {
        return selectedChainTrackId_;
    }
    RackId getSelectedChainRackId() const {
        return selectedChainRackId_;
    }
    ChainId getSelectedChainId() const {
        return selectedChainId_;
    }

    // Master channel
    const MasterChannelState& getMasterChannel() const {
        return masterChannel_;
    }
    void setMasterVolume(float volume);
    void setMasterPan(float pan);
    void setMasterMuted(bool muted);
    void setMasterSoloed(bool soloed);
    void setMasterVisible(ViewMode mode, bool visible);

    // Listener management
    void addListener(TrackManagerListener* listener);
    void removeListener(TrackManagerListener* listener);

    // Modulation management
    void notifyModulationChanged();  // Called when mod values change (for UI refresh)

    // Initialize with default tracks
    void createDefaultTracks(int count = 8);
    void clearAllTracks();

    /**
     * @brief Scan all tracks and update ID counters to avoid collisions
     *
     * After restoring tracks from a file, call this to ensure device/rack/chain
     * ID counters are updated to avoid reusing IDs that exist in loaded tracks.
     */
    void refreshIdCountersFromTracks();

    /**
     * @brief Notify all listeners that devices on a track changed
     *
     * Public so external systems (e.g. async plugin loading) can trigger
     * a UI refresh after deferred operations complete.
     *
     * Must be called on the message thread (fires listener callbacks that
     * update UI components).
     */
    void notifyTrackDevicesChanged(TrackId trackId);

  private:
    TrackManager();
    ~TrackManager() = default;

    // Mutable resolver — used only by the unified setters in
    // TrackManagerModulation.cpp. Kept private so external callers can't
    // reach in and mutate macros/mods without going through the setter
    // notification path.
    ChainNode resolveChainNode(const ChainNodePath& path);

    std::vector<TrackInfo> tracks_;
    std::vector<TrackManagerListener*> listeners_;
    int notifyDepth_ = 0;

    // RAII guard for safe listener iteration. While active, removeListener()
    // nullifies entries instead of erasing. On destruction, compacts the list.
    struct ScopedNotifyGuard {
        TrackManager& tm;
        ScopedNotifyGuard(TrackManager& t) : tm(t) {
            ++tm.notifyDepth_;
        }
        ~ScopedNotifyGuard() {
            if (--tm.notifyDepth_ == 0)
                tm.listeners_.erase(
                    std::remove(tm.listeners_.begin(), tm.listeners_.end(), nullptr),
                    tm.listeners_.end());
        }
    };
    // Helper: create a rack containing a single device and insert it into elements at insertIndex
    RackId createRackWithDevice(std::vector<ChainElement>& elements, int insertIndex,
                                DeviceInfo device, const juce::String& rackName);

    AudioEngine* audioEngine_ = nullptr;  // Non-owning pointer for routing operations
    int nextTrackId_ = 1;
    int nextDeviceId_ = 1;
    int nextRackId_ = 1;
    int nextChainId_ = 1;
    int nextAuxBusIndex_ = 0;
    MasterChannelState masterChannel_;
    TrackInfo masterTrack_;  // Dedicated TrackInfo for master channel (id=MASTER_TRACK_ID)
    TrackId selectedTrackId_ = INVALID_TRACK_ID;
    std::unordered_set<TrackId> selectedTrackIds_;
    TrackId selectedChainTrackId_ = INVALID_TRACK_ID;
    RackId selectedChainRackId_ = INVALID_RACK_ID;
    ChainId selectedChainId_ = INVALID_CHAIN_ID;

    // MIDI state for modulator triggers, protected by midiTriggerMutex_
    // Written from MIDI thread, read from timer thread
    std::map<TrackId, int> pendingMidiNoteOns_;   // per-track note-on count (consumed each tick)
    std::map<TrackId, int> pendingMidiNoteOffs_;  // per-track note-off count (consumed each tick)
    std::mutex midiTriggerMutex_;

    // Per-track held note count (persists across ticks, updated in updateAllMods)
    std::map<TrackId, int> midiHeldNotes_;

    // Transport state for LFO trigger sync (written from engine timer, read by mod timer)
    std::atomic<bool> transportPlaying_{false};
    std::atomic<double> transportBpm_{120.0};
    std::atomic<bool> transportJustStarted_{false};
    std::atomic<bool> transportJustLooped_{false};
    std::atomic<bool> transportJustStopped_{false};

    // SidechainTriggerBus counter tracking (read from mod timer, compared to detect new events)
    // Fixed-size arrays indexed by TrackId to avoid heap allocation on the mod timer path.
    static constexpr int kMaxBusTracks = 512;
    std::array<uint64_t, kMaxBusTracks> lastBusNoteOn_{};
    std::array<uint64_t, kMaxBusTracks> lastBusNoteOff_{};

    void notifyTracksChanged();
    void notifyTrackPropertyChanged(int trackId);
    void notifyMasterChannelChanged();
    void notifyTrackSelectionChanged(TrackId trackId);
    void notifyDeviceModifiersChanged(TrackId trackId);
    void notifyAudioSidechainTriggered(TrackId sourceTrackId);
    void notifyDevicePropertyChanged(DeviceId deviceId);
    void notifyDeviceParameterChanged(DeviceId deviceId, int paramIndex, float newValue);
    void notifyMacroValueChanged(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                                 float value);
    void notifyModParameterChanged(TrackId trackId, const ChainNodePath& devicePath, ModId modId,
                                   int paramIndex, float value);

    // Helper for recursive mod updates
    void updateRackMods(const RackInfo& rack, double deltaTime);

    juce::String generateTrackName() const;
};

}  // namespace magda
