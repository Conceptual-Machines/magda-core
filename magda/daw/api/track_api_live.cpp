#include "track_api_live.hpp"

#include "../core/TrackManager.hpp"

namespace magda {

TrackId TrackApiLive::createTrack(const juce::String& name, TrackType type) {
    return TrackManager::getInstance().createTrack(name, type);
}

void TrackApiLive::deleteTrack(TrackId trackId) {
    TrackManager::getInstance().deleteTrack(trackId);
}

int TrackApiLive::getNumTracks() const {
    return TrackManager::getInstance().getNumTracks();
}

const std::vector<TrackInfo>& TrackApiLive::getTracks() const {
    return TrackManager::getInstance().getTracks();
}

TrackInfo* TrackApiLive::getTrack(TrackId trackId) {
    return TrackManager::getInstance().getTrack(trackId);
}

const TrackInfo* TrackApiLive::getTrack(TrackId trackId) const {
    return TrackManager::getInstance().getTrack(trackId);
}

void TrackApiLive::setTrackName(TrackId trackId, const juce::String& name) {
    TrackManager::getInstance().setTrackName(trackId, name);
}

void TrackApiLive::setTrackVolume(TrackId trackId, float volume, bool fromAutomation) {
    TrackManager::getInstance().setTrackVolume(trackId, volume, fromAutomation);
}

void TrackApiLive::setTrackPan(TrackId trackId, float pan, bool fromAutomation) {
    TrackManager::getInstance().setTrackPan(trackId, pan, fromAutomation);
}

void TrackApiLive::setTrackMuted(TrackId trackId, bool muted) {
    TrackManager::getInstance().setTrackMuted(trackId, muted);
}

void TrackApiLive::setTrackSoloed(TrackId trackId, bool soloed) {
    TrackManager::getInstance().setTrackSoloed(trackId, soloed);
}

DeviceId TrackApiLive::addDeviceToTrack(TrackId trackId, const DeviceInfo& device) {
    return TrackManager::getInstance().addDeviceToTrack(trackId, device);
}

AudioEngine* TrackApiLive::getAudioEngine() const {
    return TrackManager::getInstance().getAudioEngine();
}

}  // namespace magda
