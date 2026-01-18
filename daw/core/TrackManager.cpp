#include "TrackManager.hpp"

#include <algorithm>

namespace magica {

TrackManager& TrackManager::getInstance() {
    static TrackManager instance;
    return instance;
}

TrackManager::TrackManager() {
    // App starts with no tracks - user can add via Track > Add Track
}

int TrackManager::createTrack(const juce::String& name) {
    TrackInfo track;
    track.id = nextTrackId_++;
    track.name = name.isEmpty() ? generateTrackName() : name;
    track.colour = TrackInfo::getDefaultColor(static_cast<int>(tracks_.size()));

    tracks_.push_back(track);
    notifyTracksChanged();

    DBG("Created track: " << track.name << " (id=" << track.id << ")");
    return track.id;
}

void TrackManager::deleteTrack(int trackId) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it != tracks_.end()) {
        DBG("Deleted track: " << it->name << " (id=" << trackId << ")");
        tracks_.erase(it);
        notifyTracksChanged();
    }
}

void TrackManager::duplicateTrack(int trackId) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });

    if (it != tracks_.end()) {
        TrackInfo newTrack = *it;
        newTrack.id = nextTrackId_++;
        newTrack.name = it->name + " Copy";

        // Insert after the original
        auto insertPos = it + 1;
        tracks_.insert(insertPos, newTrack);
        notifyTracksChanged();

        DBG("Duplicated track: " << newTrack.name << " (id=" << newTrack.id << ")");
    }
}

void TrackManager::moveTrack(int trackId, int newIndex) {
    int currentIndex = getTrackIndex(trackId);
    if (currentIndex < 0 || newIndex < 0 || newIndex >= static_cast<int>(tracks_.size())) {
        return;
    }

    if (currentIndex != newIndex) {
        TrackInfo track = tracks_[currentIndex];
        tracks_.erase(tracks_.begin() + currentIndex);
        tracks_.insert(tracks_.begin() + newIndex, track);
        notifyTracksChanged();
    }
}

TrackInfo* TrackManager::getTrack(int trackId) {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });
    return (it != tracks_.end()) ? &(*it) : nullptr;
}

const TrackInfo* TrackManager::getTrack(int trackId) const {
    auto it = std::find_if(tracks_.begin(), tracks_.end(),
                           [trackId](const TrackInfo& t) { return t.id == trackId; });
    return (it != tracks_.end()) ? &(*it) : nullptr;
}

int TrackManager::getTrackIndex(int trackId) const {
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].id == trackId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TrackManager::setTrackName(int trackId, const juce::String& name) {
    if (auto* track = getTrack(trackId)) {
        track->name = name;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackColour(int trackId, juce::Colour colour) {
    if (auto* track = getTrack(trackId)) {
        track->colour = colour;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackVolume(int trackId, float volume) {
    if (auto* track = getTrack(trackId)) {
        track->volume = juce::jlimit(0.0f, 1.0f, volume);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackPan(int trackId, float pan) {
    if (auto* track = getTrack(trackId)) {
        track->pan = juce::jlimit(-1.0f, 1.0f, pan);
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackMuted(int trackId, bool muted) {
    if (auto* track = getTrack(trackId)) {
        track->muted = muted;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackSoloed(int trackId, bool soloed) {
    if (auto* track = getTrack(trackId)) {
        track->soloed = soloed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::setTrackRecordArmed(int trackId, bool armed) {
    if (auto* track = getTrack(trackId)) {
        track->recordArmed = armed;
        notifyTrackPropertyChanged(trackId);
    }
}

void TrackManager::addListener(TrackManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void TrackManager::removeListener(TrackManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void TrackManager::createDefaultTracks(int count) {
    clearAllTracks();
    for (int i = 0; i < count; ++i) {
        createTrack();
    }
}

void TrackManager::clearAllTracks() {
    tracks_.clear();
    nextTrackId_ = 1;
    notifyTracksChanged();
}

void TrackManager::notifyTracksChanged() {
    for (auto* listener : listeners_) {
        listener->tracksChanged();
    }
}

void TrackManager::notifyTrackPropertyChanged(int trackId) {
    for (auto* listener : listeners_) {
        listener->trackPropertyChanged(trackId);
    }
}

juce::String TrackManager::generateTrackName() const {
    return juce::String(tracks_.size() + 1) + " Track";
}

}  // namespace magica
