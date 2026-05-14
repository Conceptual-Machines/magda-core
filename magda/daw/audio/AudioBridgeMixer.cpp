#include "AudioBridgeMixer.hpp"

#include "TrackController.hpp"

namespace magda {

AudioBridgeMixer::AudioBridgeMixer(te::Edit& edit, TrackController& trackController)
    : edit_(edit), trackController_(trackController) {}

void AudioBridgeMixer::setTrackVolume(TrackId trackId, float volume) {
    trackController_.setTrackVolume(trackId, volume);
}

float AudioBridgeMixer::getTrackVolume(TrackId trackId) const {
    return trackController_.getTrackVolume(trackId);
}

void AudioBridgeMixer::setTrackPan(TrackId trackId, float pan) {
    trackController_.setTrackPan(trackId, pan);
}

float AudioBridgeMixer::getTrackPan(TrackId trackId) const {
    return trackController_.getTrackPan(trackId);
}

void AudioBridgeMixer::setMasterVolume(float volume) {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        masterPlugin->setVolumeDb(db);
    }
}

float AudioBridgeMixer::getMasterVolume() const {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin)
        return juce::Decibels::decibelsToGain(masterPlugin->getVolumeDb());

    return 1.0f;
}

void AudioBridgeMixer::setMasterPan(float pan) {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin)
        masterPlugin->setPan(pan);
}

float AudioBridgeMixer::getMasterPan() const {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin)
        return masterPlugin->getPan();

    return 0.0f;
}

}  // namespace magda
