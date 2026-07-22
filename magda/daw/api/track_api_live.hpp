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

    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device) override;
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

    AudioEngine* getAudioEngine() const override;
};

}  // namespace magda
