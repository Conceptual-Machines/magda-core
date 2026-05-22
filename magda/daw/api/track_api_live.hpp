#pragma once

#include "track_api.hpp"

namespace magda {

/// Forwards every TrackApi call to TrackManager::getInstance().
class TrackApiLive : public TrackApi {
  public:
    TrackId createTrack(const juce::String& name, TrackType type) override;
    void deleteTrack(TrackId trackId) override;

    int getNumTracks() const override;
    const std::vector<TrackInfo>& getTracks() const override;
    TrackInfo* getTrack(TrackId trackId) override;
    const TrackInfo* getTrack(TrackId trackId) const override;

    void setTrackName(TrackId trackId, const juce::String& name) override;
    void setTrackVolume(TrackId trackId, float volume, bool fromAutomation) override;
    void setTrackPan(TrackId trackId, float pan, bool fromAutomation) override;
    void setTrackMuted(TrackId trackId, bool muted) override;
    void setTrackSoloed(TrackId trackId, bool soloed) override;

    DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device) override;
    const DeviceInfo* getPrimaryInstrument(TrackId trackId) const override;

    AudioEngine* getAudioEngine() const override;
};

}  // namespace magda
