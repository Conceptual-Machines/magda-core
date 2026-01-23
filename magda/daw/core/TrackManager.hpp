#pragma once

#include <memory>
#include <vector>

#include "SelectionManager.hpp"
#include "TrackInfo.hpp"
#include "TrackTypes.hpp"
#include "ViewModeState.hpp"

namespace magda {

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
};

/**
 * @brief Singleton manager for all tracks in the project
 *
 * Provides CRUD operations for tracks and notifies listeners of changes.
 */
class TrackManager {
  public:
    static TrackManager& getInstance();

    // Prevent copying
    TrackManager(const TrackManager&) = delete;
    TrackManager& operator=(const TrackManager&) = delete;

    // Track operations
    TrackId createTrack(const juce::String& name = "", TrackType type = TrackType::Audio);
    TrackId createGroupTrack(const juce::String& name = "");
    void deleteTrack(TrackId trackId);
    void duplicateTrack(TrackId trackId);
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
    void setTrackVolume(TrackId trackId, float volume);
    void setTrackPan(TrackId trackId, float pan);
    void setTrackMuted(TrackId trackId, bool muted);
    void setTrackSoloed(TrackId trackId, bool soloed);
    void setTrackRecordArmed(TrackId trackId, bool armed);
    void setTrackType(TrackId trackId, TrackType type);

    // View settings
    void setTrackVisible(TrackId trackId, ViewMode mode, bool visible);
    void setTrackLocked(TrackId trackId, ViewMode mode, bool locked);
    void setTrackCollapsed(TrackId trackId, ViewMode mode, bool collapsed);
    void setTrackHeight(TrackId trackId, ViewMode mode, int height);

    // Signal chain management (unified list of devices and racks)
    const std::vector<ChainElement>& getChainElements(TrackId trackId) const;
    void moveNode(TrackId trackId, int fromIndex, int toIndex);

    // Device management on track
    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device);
    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device, int insertIndex);
    void removeDeviceFromTrack(TrackId trackId, DeviceId deviceId);
    void setDeviceBypassed(TrackId trackId, DeviceId deviceId, bool bypassed);
    DeviceInfo* getDevice(TrackId trackId, DeviceId deviceId);

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
    void setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo);
    void setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volume);
    void setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan);
    void setChainExpanded(TrackId trackId, RackId rackId, ChainId chainId, bool expanded);

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
    void setDeviceInChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                  DeviceId deviceId, bool bypassed);
    void setDeviceInChainBypassedByPath(const ChainNodePath& devicePath, bool bypassed);

    // Nested rack management within chains
    RackId addRackToChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                          const juce::String& name = "Rack");
    RackId addRackToChainByPath(const ChainNodePath& chainPath, const juce::String& name = "Rack");
    void removeRackFromChain(TrackId trackId, RackId parentRackId, ChainId chainId,
                             RackId nestedRackId);
    void removeRackFromChainByPath(const ChainNodePath& rackPath);

    // Macro management for racks (path-based for nested rack support)
    void setRackMacroValue(const ChainNodePath& rackPath, int macroIndex, float value);
    void setRackMacroTarget(const ChainNodePath& rackPath, int macroIndex, MacroTarget target);
    void setRackMacroLinkAmount(const ChainNodePath& rackPath, int macroIndex, MacroTarget target,
                                float amount);
    void setRackMacroName(const ChainNodePath& rackPath, int macroIndex, const juce::String& name);
    void addRackMacroPage(const ChainNodePath& rackPath);
    void removeRackMacroPage(const ChainNodePath& rackPath);

    // Mod management for racks (path-based for nested rack support)
    void setRackModAmount(const ChainNodePath& rackPath, int modIndex, float amount);
    void setRackModTarget(const ChainNodePath& rackPath, int modIndex, ModTarget target);
    void setRackModName(const ChainNodePath& rackPath, int modIndex, const juce::String& name);
    void setRackModType(const ChainNodePath& rackPath, int modIndex, ModType type);
    void setRackModWaveform(const ChainNodePath& rackPath, int modIndex, LFOWaveform waveform);
    void setRackModRate(const ChainNodePath& rackPath, int modIndex, float rate);
    void addRackModPage(const ChainNodePath& rackPath);
    void removeRackModPage(const ChainNodePath& rackPath);

    // Mod management for devices (path-based for nested device support)
    void setDeviceModAmount(const ChainNodePath& devicePath, int modIndex, float amount);
    void setDeviceModTarget(const ChainNodePath& devicePath, int modIndex, ModTarget target);
    void removeDeviceModLink(const ChainNodePath& devicePath, int modIndex, ModTarget target);
    void setDeviceModLinkAmount(const ChainNodePath& devicePath, int modIndex, ModTarget target,
                                float amount);
    void setDeviceModName(const ChainNodePath& devicePath, int modIndex, const juce::String& name);
    void setDeviceModType(const ChainNodePath& devicePath, int modIndex, ModType type);
    void setDeviceModWaveform(const ChainNodePath& devicePath, int modIndex, LFOWaveform waveform);
    void setDeviceModRate(const ChainNodePath& devicePath, int modIndex, float rate);
    void addDeviceModPage(const ChainNodePath& devicePath);
    void removeDeviceModPage(const ChainNodePath& devicePath);

    // Macro management for devices (path-based for nested device support)
    void setDeviceMacroValue(const ChainNodePath& devicePath, int macroIndex, float value);
    void setDeviceMacroTarget(const ChainNodePath& devicePath, int macroIndex, MacroTarget target);
    void removeDeviceMacroLink(const ChainNodePath& devicePath, int macroIndex, MacroTarget target);
    void setDeviceMacroLinkAmount(const ChainNodePath& devicePath, int macroIndex,
                                  MacroTarget target, float amount);
    void setDeviceMacroName(const ChainNodePath& devicePath, int macroIndex,
                            const juce::String& name);
    void addDeviceMacroPage(const ChainNodePath& devicePath);
    void removeDeviceMacroPage(const ChainNodePath& devicePath);

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
    void updateAllMods(double deltaTime);  // Update all mod values (called by ModulatorEngine)
    void notifyModulationChanged();        // Called when mod values change (for UI refresh)

    // Initialize with default tracks
    void createDefaultTracks(int count = 8);
    void clearAllTracks();

  private:
    TrackManager();
    ~TrackManager() = default;

    std::vector<TrackInfo> tracks_;
    std::vector<TrackManagerListener*> listeners_;
    int nextTrackId_ = 1;
    int nextDeviceId_ = 1;
    int nextRackId_ = 1;
    int nextChainId_ = 1;
    MasterChannelState masterChannel_;
    TrackId selectedTrackId_ = INVALID_TRACK_ID;
    TrackId selectedChainTrackId_ = INVALID_TRACK_ID;
    RackId selectedChainRackId_ = INVALID_RACK_ID;
    ChainId selectedChainId_ = INVALID_CHAIN_ID;

    void notifyTracksChanged();
    void notifyTrackPropertyChanged(int trackId);
    void notifyMasterChannelChanged();
    void notifyTrackSelectionChanged(TrackId trackId);
    void notifyTrackDevicesChanged(TrackId trackId);

    // Helper for recursive mod updates
    void updateRackMods(RackInfo& rack, double deltaTime);

    juce::String generateTrackName() const;
};

}  // namespace magda
