#pragma once

#include "track_api.hpp"

namespace magda {

/// Forwards every TrackApi call to TrackManager::getInstance().
class TrackApiLive : public TrackApi {
  public:
    TrackId createTrack(const juce::String& name, TrackType type) override;
    TrackId groupTracks(const std::vector<TrackId>& trackIds, const juce::String& name) override;
    void deleteTrack(TrackId trackId) override;
    void moveTrackToPosition(TrackId trackId, int oneBasedPosition) override;

    int getNumTracks() const override;
    const std::vector<TrackInfo>& getTracks() const override;
    TrackInfo* getTrack(TrackId trackId) override;
    const TrackInfo* getTrack(TrackId trackId) const override;

    void setTrackName(TrackId trackId, const juce::String& name) override;
    void setTrackColour(TrackId trackId, juce::Colour colour) override;
    void setTrackVolume(TrackId trackId, float volume, bool fromAutomation) override;
    void setTrackPan(TrackId trackId, float pan, bool fromAutomation) override;
    void setTrackMuted(TrackId trackId, bool muted) override;
    void setTrackSoloed(TrackId trackId, bool soloed) override;
    void setTrackRecordArmed(TrackId trackId, bool armed) override;

    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device) override;
    DeviceId addDeviceToChain(TrackId trackId, RackId rackId, ChainId chainId,
                              const DeviceInfo& device) override;

    // Path-addressed racks and chains (#1993). These are the implementations;
    // the triple-based overrides below are shims over them.
    RackId addRackToChainByPath(const ChainNodePath& chainPath, const juce::String& name) override;
    void removeRackFromChainByPath(const ChainNodePath& rackPath) override;
    const RackInfo* getRackByPath(const ChainNodePath& rackPath) const override;
    void setRackBypassedByPath(const ChainNodePath& rackPath, bool bypassed) override;
    void setRackVolume(const ChainNodePath& rackPath, float volumeDb) override;
    ChainId addChainToRack(const ChainNodePath& rackPath, const juce::String& name) override;
    void removeChainByPath(const ChainNodePath& chainPath) override;
    const ChainInfo* getChainByPath(const ChainNodePath& chainPath) const override;
    void setChainOutput(const ChainNodePath& chainPath, int outputIndex) override;
    void setChainMuted(const ChainNodePath& chainPath, bool muted) override;
    void setChainBypassed(const ChainNodePath& chainPath, bool bypassed) override;
    void setChainSolo(const ChainNodePath& chainPath, bool solo) override;
    void setChainVolume(const ChainNodePath& chainPath, float volumeDb) override;
    void setChainPan(const ChainNodePath& chainPath, float pan) override;
    void setChainName(const ChainNodePath& chainPath, const juce::String& name) override;
    DeviceId addDeviceToChainByPath(const ChainNodePath& chainPath,
                                    const DeviceInfo& device) override;

    RackId addRackToTrack(TrackId trackId, const juce::String& name) override;
    void removeRackFromTrack(TrackId trackId, RackId rackId) override;
    const RackInfo* getRack(TrackId trackId, RackId rackId) const override;
    void setRackBypassed(TrackId trackId, RackId rackId, bool bypassed) override;
    void setRackVolume(TrackId trackId, RackId rackId, float volumeDb) override;
    ChainId addChainToRack(TrackId trackId, RackId rackId, const juce::String& name) override;
    void removeChainFromRack(TrackId trackId, RackId rackId, ChainId chainId) override;
    const ChainInfo* getChain(TrackId trackId, RackId rackId, ChainId chainId) const override;
    void setChainOutput(TrackId trackId, RackId rackId, ChainId chainId, int outputIndex) override;
    void setChainMuted(TrackId trackId, RackId rackId, ChainId chainId, bool muted) override;
    void setChainBypassed(TrackId trackId, RackId rackId, ChainId chainId, bool bypassed) override;
    void setChainSolo(TrackId trackId, RackId rackId, ChainId chainId, bool solo) override;
    void setChainVolume(TrackId trackId, RackId rackId, ChainId chainId, float volumeDb) override;
    void setChainPan(TrackId trackId, RackId rackId, ChainId chainId, float pan) override;
    void setChainName(TrackId trackId, RackId rackId, ChainId chainId,
                      const juce::String& name) override;
    const DeviceInfo* getPrimaryInstrument(TrackId trackId) const override;
};

}  // namespace magda
