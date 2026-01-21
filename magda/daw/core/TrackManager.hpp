#pragma once

#include <memory>
#include <vector>

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

    // Device/FX chain management (flat device list on track)
    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device);
    void removeDeviceFromTrack(TrackId trackId, DeviceId deviceId);
    void moveDevice(TrackId trackId, DeviceId deviceId, int newIndex);
    void setDeviceBypassed(TrackId trackId, DeviceId deviceId, bool bypassed);
    const std::vector<DeviceInfo>* getDevices(TrackId trackId) const;
    DeviceInfo* getDevice(TrackId trackId, DeviceId deviceId);

    // Rack management
    RackId addRackToTrack(TrackId trackId, const juce::String& name = "Rack");
    void removeRackFromTrack(TrackId trackId, RackId rackId);
    RackInfo* getRack(TrackId trackId, RackId rackId);
    const RackInfo* getRack(TrackId trackId, RackId rackId) const;
    const std::vector<RackInfo>* getRacks(TrackId trackId) const;
    void setRackBypassed(TrackId trackId, RackId rackId, bool bypassed);
    void setRackExpanded(TrackId trackId, RackId rackId, bool expanded);

    // Chain management (within racks)
    ChainId addChainToRack(TrackId trackId, RackId rackId, const juce::String& name = "Chain");
    void removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId);
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
    void removeDeviceFromChain(TrackId trackId, RackId rackId, ChainId chainId, DeviceId deviceId);
    void moveDeviceInChain(TrackId trackId, RackId rackId, ChainId chainId, DeviceId deviceId,
                           int newIndex);
    DeviceInfo* getDeviceInChain(TrackId trackId, RackId rackId, ChainId chainId,
                                 DeviceId deviceId);
    void setDeviceInChainBypassed(TrackId trackId, RackId rackId, ChainId chainId,
                                  DeviceId deviceId, bool bypassed);

    // Query tracks by view
    std::vector<TrackId> getVisibleTracks(ViewMode mode) const;
    std::vector<TrackId> getVisibleTopLevelTracks(ViewMode mode) const;

    // Track selection
    void setSelectedTrack(TrackId trackId);
    TrackId getSelectedTrack() const {
        return selectedTrackId_;
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

    void notifyTracksChanged();
    void notifyTrackPropertyChanged(int trackId);
    void notifyMasterChannelChanged();
    void notifyTrackSelectionChanged(TrackId trackId);
    void notifyTrackDevicesChanged(TrackId trackId);

    juce::String generateTrackName() const;
};

}  // namespace magda
