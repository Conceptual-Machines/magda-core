#include "MixerController.hpp"

namespace magda {

namespace {
// Helper to find AudioTrack from track mapping
te::AudioTrack* findAudioTrack(const std::map<TrackId, te::AudioTrack*>& trackMapping,
                               TrackId trackId) {
    auto it = trackMapping.find(trackId);
    return it != trackMapping.end() ? it->second : nullptr;
}
}  // namespace

void MixerController::setTrackVolume(te::Edit& edit,
                                     const std::map<TrackId, te::AudioTrack*>& trackMapping,
                                     TrackId trackId, float volume) {
    auto* track = findAudioTrack(trackMapping, trackId);
    if (!track)
        return;

    // Use the track's volume plugin (positioned at end of chain before LevelMeter)
    if (auto* volPan = track->getVolumePlugin()) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        volPan->setVolumeDb(db);
    }
}

float MixerController::getTrackVolume(te::Edit& edit,
                                      const std::map<TrackId, te::AudioTrack*>& trackMapping,
                                      TrackId trackId) const {
    auto* track = findAudioTrack(trackMapping, trackId);
    if (!track) {
        return 1.0f;
    }

    if (auto* volPan = track->getVolumePlugin()) {
        return juce::Decibels::decibelsToGain(volPan->getVolumeDb());
    }
    return 1.0f;
}

void MixerController::setTrackPan(te::Edit& edit,
                                  const std::map<TrackId, te::AudioTrack*>& trackMapping,
                                  TrackId trackId, float pan) {
    auto* track = findAudioTrack(trackMapping, trackId);
    if (!track) {
        DBG("MixerController::setTrackPan - track not found: " << trackId);
        return;
    }

    // Use the track's built-in volume plugin
    if (auto* volPan = track->getVolumePlugin()) {
        volPan->setPan(pan);
    }
}

float MixerController::getTrackPan(te::Edit& edit,
                                   const std::map<TrackId, te::AudioTrack*>& trackMapping,
                                   TrackId trackId) const {
    auto* track = findAudioTrack(trackMapping, trackId);
    if (!track) {
        return 0.0f;
    }

    if (auto* volPan = track->getVolumePlugin()) {
        return volPan->getPan();
    }
    return 0.0f;
}

void MixerController::setMasterVolume(te::Edit& edit, float volume) {
    if (auto masterPlugin = edit.getMasterVolumePlugin()) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        masterPlugin->setVolumeDb(db);
    }
}

float MixerController::getMasterVolume(te::Edit& edit) const {
    if (auto masterPlugin = edit.getMasterVolumePlugin()) {
        return juce::Decibels::decibelsToGain(masterPlugin->getVolumeDb());
    }
    return 1.0f;
}

void MixerController::setMasterPan(te::Edit& edit, float pan) {
    if (auto masterPlugin = edit.getMasterVolumePlugin()) {
        masterPlugin->setPan(pan);
    }
}

float MixerController::getMasterPan(te::Edit& edit) const {
    if (auto masterPlugin = edit.getMasterVolumePlugin()) {
        return masterPlugin->getPan();
    }
    return 0.0f;
}

}  // namespace magda
