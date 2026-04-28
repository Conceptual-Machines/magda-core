#pragma once

#include <vector>

#include "../core/DeviceInfo.hpp"
#include "../core/TrackInfo.hpp"
#include "../core/TrackTypes.hpp"
#include "../core/TypeIds.hpp"

namespace magda {

class AudioEngine;

/**
 * Abstract view onto TrackManager — the track-level surface the agent
 * layer needs.
 *
 * NOTE: getAudioEngine() returns te::Engine*-equivalent and leaks the
 * engine into the API surface. Acceptable for now (executors need it
 * for groove/bpm + plugin scanning), but flagged: a future
 * MagdaApiPlugin variant probably won't return a real engine here.
 */
class TrackApi {
  public:
    virtual ~TrackApi() = default;

    virtual TrackId createTrack(const juce::String& name, TrackType type) = 0;
    virtual void deleteTrack(TrackId trackId) = 0;

    virtual int getNumTracks() const = 0;
    virtual const std::vector<TrackInfo>& getTracks() const = 0;
    virtual TrackInfo* getTrack(TrackId trackId) = 0;
    virtual const TrackInfo* getTrack(TrackId trackId) const = 0;

    virtual void setTrackName(TrackId trackId, const juce::String& name) = 0;
    virtual void setTrackVolume(TrackId trackId, float volume, bool fromAutomation = false) = 0;
    virtual void setTrackPan(TrackId trackId, float pan, bool fromAutomation = false) = 0;
    virtual void setTrackMuted(TrackId trackId, bool muted) = 0;
    virtual void setTrackSoloed(TrackId trackId, bool soloed) = 0;

    virtual DeviceId addDeviceToTrack(TrackId trackId, const DeviceInfo& device) = 0;

    virtual AudioEngine* getAudioEngine() const = 0;
};

}  // namespace magda
