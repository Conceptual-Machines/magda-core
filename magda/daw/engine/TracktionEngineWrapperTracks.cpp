#include "../audio/AudioBridge.hpp"
#include "../core/TrackManager.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda {

std::string TracktionEngineWrapper::createAudioTrack(const std::string& name) {
    if (!currentEdit_)
        return "";

    auto trackId = generateTrackId();
    auto tracks = tracktion::getAudioTracks(*currentEdit_);
    auto insertPoint = tracktion::TrackInsertPoint(nullptr, nullptr);
    auto track = currentEdit_->insertNewAudioTrack(insertPoint, nullptr);
    if (track) {
        track->setName(name);
        trackMap_[trackId] = track;
        DBG("Created audio track: " << name << " (ID: " << trackId << ")");
    }
    return trackId;
}

std::string TracktionEngineWrapper::createMidiTrack(const std::string& name) {
    if (!currentEdit_)
        return "";

    auto trackId = generateTrackId();
    auto insertPoint = tracktion::TrackInsertPoint(nullptr, nullptr);
    auto track = currentEdit_->insertNewAudioTrack(insertPoint, nullptr);
    if (track) {
        track->setName(name);
        trackMap_[trackId] = track;
        DBG("Created MIDI track: " << name << " (ID: " << trackId << ")");
    }
    return trackId;
}

void TracktionEngineWrapper::deleteTrack(const std::string& track_id) {
    auto it = trackMap_.find(track_id);
    if (it != trackMap_.end() && currentEdit_) {
        currentEdit_->deleteTrack(it->second.get());
        trackMap_.erase(it);
        DBG("Deleted track ID: " << track_id);
    }
}

void TracktionEngineWrapper::setTrackName(const std::string& track_id, const std::string& name) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setName(name);
    }
}

std::string TracktionEngineWrapper::getTrackName(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->getName().toStdString() : "";
}

void TracktionEngineWrapper::setTrackMuted(const std::string& track_id, bool muted) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setMute(muted);
    }
}

bool TracktionEngineWrapper::isTrackMuted(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->isMuted(false) : false;
}

void TracktionEngineWrapper::setTrackSolo(const std::string& track_id, bool solo) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setSolo(solo);
    }
}

bool TracktionEngineWrapper::isTrackSolo(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    return track ? track->isSolo(false) : false;
}

void TracktionEngineWrapper::setTrackArmed(const std::string& track_id, bool armed) {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Simplified - in real implementation would set input recording
            DBG("Set track armed (stub): " << track_id << " = " << (int)armed);
        }
    }
}

bool TracktionEngineWrapper::isTrackArmed(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto audioTrack = dynamic_cast<tracktion::AudioTrack*>(track)) {
            // Simplified - in real implementation would check input recording
            return false;
        }
    }
    return false;
}

void TracktionEngineWrapper::setTrackColor(const std::string& track_id, int r, int g, int b) {
    auto track = findTrackById(track_id);
    if (track) {
        track->setColour(juce::Colour::fromRGB(r, g, b));
    }
}

std::vector<std::string> TracktionEngineWrapper::getAllTrackIds() const {
    std::vector<std::string> ids;
    for (const auto& pair : trackMap_) {
        ids.push_back(pair.first);
    }
    return ids;
}

bool TracktionEngineWrapper::trackExists(const std::string& track_id) const {
    return trackMap_.find(track_id) != trackMap_.end();
}

void TracktionEngineWrapper::previewNoteOnTrack(const std::string& track_id, int noteNumber,
                                                int velocity, bool isNoteOn) {
    DBG("TracktionEngineWrapper::previewNoteOnTrack - Track="
        << track_id << ", Note=" << noteNumber << ", Velocity=" << velocity
        << ", On=" << (isNoteOn ? "YES" : "NO"));

    if (!audioBridge_) {
        DBG("TracktionEngineWrapper: WARNING - No AudioBridge!");
        return;
    }

    // Convert string track ID to integer (MAGDA TrackId) with validation
    int magdaTrackId = 0;
    try {
        magdaTrackId = std::stoi(track_id);
    } catch (const std::exception& e) {
        DBG("TracktionEngineWrapper: WARNING - Invalid track ID '"
            << track_id << "' passed to previewNoteOnTrack: " << e.what());
        return;
    }
    DBG("TracktionEngineWrapper: Looking up MAGDA track ID: " << magdaTrackId);

    // Use AudioBridge to get the Tracktion AudioTrack
    auto* audioTrack = audioBridge_->getAudioTrack(magdaTrackId);
    if (!audioTrack) {
        DBG("TracktionEngineWrapper: WARNING - Track not found in AudioBridge!");
        return;
    }

    // Inject MIDI directly into the track's plugin chain via LiveMidiInjectingNode,
    // bypassing the MIDI input device and its monitor mode entirely (#762)
    juce::MidiMessage message =
        isNoteOn ? juce::MidiMessage::noteOn(1, noteNumber, (juce::uint8)velocity)
                 : juce::MidiMessage::noteOff(1, noteNumber, (juce::uint8)velocity);

    audioTrack->injectLiveMidiMessage(message, {});
}

void TracktionEngineWrapper::setTrackVolume(const std::string& track_id, double volume) {
    auto track = findTrackById(track_id);
    if (track) {
        // Find VolumeAndPanPlugin on track
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            // Convert linear gain to dB for the plugin
            float db =
                volume > 0.0 ? static_cast<float>(juce::Decibels::gainToDecibels(volume)) : -100.0f;
            volPan->setVolumeDb(db);
        } else {
            // No VolumeAndPanPlugin found - this shouldn't happen as Tracktion auto-creates one
            DBG("Warning: No VolumeAndPanPlugin on track " << track_id);
        }
    }
}

double TracktionEngineWrapper::getTrackVolume(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            float db = volPan->getVolumeDb();
            return juce::Decibels::decibelsToGain(db);
        }
    }
    return 1.0;
}

void TracktionEngineWrapper::setTrackPan(const std::string& track_id, double pan) {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            // Pan is -1.0 (left) to 1.0 (right)
            volPan->setPan(static_cast<float>(pan));
        }
    }
}

double TracktionEngineWrapper::getTrackPan(const std::string& track_id) const {
    auto track = findTrackById(track_id);
    if (track) {
        if (auto volPan =
                track->pluginList.findFirstPluginOfType<tracktion::VolumeAndPanPlugin>()) {
            return volPan->getPan();
        }
    }
    return 0.0;
}

void TracktionEngineWrapper::setMasterVolume(double volume) {
    if (currentEdit_) {
        currentEdit_->getMasterVolumePlugin()->setVolumeDb(juce::Decibels::gainToDecibels(volume));
    }
}

double TracktionEngineWrapper::getMasterVolume() const {
    if (currentEdit_) {
        return juce::Decibels::decibelsToGain(currentEdit_->getMasterVolumePlugin()->getVolumeDb());
    }
    return 1.0;
}

}  // namespace magda
