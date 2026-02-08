#include "TrackController.hpp"

#include <iostream>

namespace magda {

TrackController::TrackController(te::Engine& engine, te::Edit& edit)
    : engine_(engine), edit_(edit) {}

// =============================================================================
// Core Track Lifecycle
// =============================================================================

te::AudioTrack* TrackController::getAudioTrack(TrackId trackId) const {
    juce::ScopedLock lock(trackLock_);
    auto it = trackMapping_.find(trackId);
    return it != trackMapping_.end() ? it->second : nullptr;
}

te::AudioTrack* TrackController::createAudioTrack(TrackId trackId, const juce::String& name) {
    juce::ScopedLock lock(trackLock_);

    // Check if track already exists
    auto it = trackMapping_.find(trackId);
    if (it != trackMapping_.end() && it->second != nullptr) {
        return it->second;
    }

    // Create new track (must be done under lock to prevent race condition)
    auto insertPoint = te::TrackInsertPoint(nullptr, nullptr);
    auto trackPtr = edit_.insertNewAudioTrack(insertPoint, nullptr);

    te::AudioTrack* track = trackPtr.get();
    if (track) {
        track->setName(name);

        // Route track output to master/default output
        track->getOutput().setOutputToDefaultDevice(false);  // false = audio (not MIDI)

        // Register track mapping
        trackMapping_[trackId] = track;

        std::cout << "TrackController: Created Tracktion AudioTrack for MAGDA track " << trackId
                  << ": " << name << " (routed to master)" << std::endl;
    }

    return track;
}

void TrackController::removeAudioTrack(TrackId trackId) {
    te::AudioTrack* track = nullptr;

    {
        juce::ScopedLock lock(trackLock_);
        auto it = trackMapping_.find(trackId);
        if (it != trackMapping_.end()) {
            track = it->second;

            // Unregister meter client before removing track
            if (track) {
                auto* levelMeter = track->getLevelMeterPlugin();
                if (levelMeter) {
                    auto clientIt = meterClients_.find(trackId);
                    if (clientIt != meterClients_.end()) {
                        levelMeter->measurer.removeClient(clientIt->second);
                        meterClients_.erase(clientIt);
                    }
                }
            }

            trackMapping_.erase(it);
        }
    }

    // Delete track from edit (expensive operation, done outside lock)
    if (track) {
        edit_.deleteTrack(track);
        std::cout << "TrackController: Removed Tracktion AudioTrack for MAGDA track " << trackId
                  << std::endl;
    }
}

te::AudioTrack* TrackController::ensureTrackMapping(TrackId trackId, const juce::String& name) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        track = createAudioTrack(trackId, name);
    }
    return track;
}

// =============================================================================
// Mixer Controls
// =============================================================================

void TrackController::setTrackVolume(TrackId trackId, float volume) {
    auto* track = getAudioTrack(trackId);
    if (!track)
        return;

    // Use the track's volume plugin (positioned at end of chain before LevelMeter)
    if (auto* volPan = track->getVolumePlugin()) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        volPan->setVolumeDb(db);
    }
}

float TrackController::getTrackVolume(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return 1.0f;
    }

    if (auto* volPan = track->getVolumePlugin()) {
        return juce::Decibels::decibelsToGain(volPan->getVolumeDb());
    }
    return 1.0f;
}

void TrackController::setTrackPan(TrackId trackId, float pan) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("TrackController::setTrackPan - track not found: " << trackId);
        return;
    }

    // Use the track's built-in volume plugin
    if (auto* volPan = track->getVolumePlugin()) {
        volPan->setPan(pan);
    }
}

float TrackController::getTrackPan(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return 0.0f;
    }

    if (auto* volPan = track->getVolumePlugin()) {
        return volPan->getPan();
    }
    return 0.0f;
}

// =============================================================================
// Audio Routing
// =============================================================================

void TrackController::setTrackAudioOutput(TrackId trackId, const juce::String& destination) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("TrackController::setTrackAudioOutput - track not found: " << trackId);
        return;
    }

    DBG("TrackController::setTrackAudioOutput - trackId=" << trackId << " destination='"
                                                          << destination << "'");

    if (destination.isEmpty()) {
        // Disable output - mute the track
        track->setMute(true);
    } else if (destination == "master") {
        // Route to default/master output
        track->setMute(false);
        track->getOutput().setOutputToDefaultDevice(false);  // false = audio (not MIDI)
    } else {
        // Route to specific output device
        track->setMute(false);
        track->getOutput().setOutputToDeviceID(destination);
    }
}

juce::String TrackController::getTrackAudioOutput(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return {};
    }

    if (track->isMuted(false)) {
        return {};  // Muted = disabled output (matches empty string from setTrackAudioOutput)
    }

    auto& output = track->getOutput();
    if (output.usesDefaultAudioOut()) {
        return "master";  // Consistent with "master" keyword in setTrackAudioOutput
    }

    // Return the output device ID for round-trip consistency
    // Note: getOutputToDeviceID() is not available, so we use getOutputName()
    // which should match the deviceId passed to setOutputToDeviceID()
    return output.getOutputName();
}

void TrackController::setTrackAudioInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("TrackController::setTrackAudioInput - track not found: " << trackId);
        return;
    }

    DBG("TrackController::setTrackAudioInput - trackId=" << trackId << " deviceId='" << deviceId
                                                         << "'");

    if (deviceId.isEmpty()) {
        // Disable input - clear all assignments
        auto* playbackContext = edit_.getCurrentPlaybackContext();
        if (playbackContext) {
            for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
                if (!result) {
                    DBG("  -> Warning: Could not remove audio input target - "
                        << result.getErrorMessage());
                }
            }
        }
        DBG("  -> Cleared audio input");
    } else {
        // Enable input - route default or specific device to this track
        auto* playbackContext = edit_.getCurrentPlaybackContext();
        if (playbackContext) {
            auto allInputs = playbackContext->getAllInputs();

            if (deviceId == "default") {
                // Use first available audio (non-MIDI) input device
                for (auto* input : allInputs) {
                    if (dynamic_cast<te::MidiInputDevice*>(&input->owner))
                        continue;
                    auto result = input->setTarget(track->itemID, false, nullptr);
                    if (result.has_value()) {
                        (*result)->recordEnabled = false;  // Don't auto-enable recording
                        DBG("  -> Routed default audio input to track");
                        break;
                    }
                }
            } else {
                // Find specific device by name and route it
                for (auto* inputDeviceInstance : allInputs) {
                    if (inputDeviceInstance->owner.getName() == deviceId) {
                        auto result = inputDeviceInstance->setTarget(track->itemID, false, nullptr);
                        if (result.has_value()) {
                            (*result)->recordEnabled = false;
                            DBG("  -> Routed input '" << deviceId << "' to track");
                        }
                        break;
                    }
                }
            }
        }
    }
}

juce::String TrackController::getTrackAudioInput(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return {};
    }

    // Check if any input device is routed to this track
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (playbackContext) {
        auto allInputs = playbackContext->getAllInputs();
        for (int i = 0; i < allInputs.size(); ++i) {
            auto* inputDeviceInstance = allInputs[i];
            auto targets = inputDeviceInstance->getTargets();
            for (auto targetID : targets) {
                if (targetID == track->itemID) {
                    // Return "default" if this is the first input (for round-trip consistency)
                    if (i == 0) {
                        return "default";
                    }
                    return inputDeviceInstance->owner.getName();
                }
            }
        }
    }

    return {};  // No input assigned (matches empty string from setTrackAudioInput)
}

// =============================================================================
// Utilities
// =============================================================================

std::vector<TrackId> TrackController::getAllTrackIds() const {
    juce::ScopedLock lock(trackLock_);
    std::vector<TrackId> trackIds;
    trackIds.reserve(trackMapping_.size());
    for (const auto& [trackId, track] : trackMapping_) {
        trackIds.push_back(trackId);
    }
    return trackIds;
}

void TrackController::clearAllMappings() {
    juce::ScopedLock lock(trackLock_);
    trackMapping_.clear();
    meterClients_.clear();
}

void TrackController::withTrackMapping(
    std::function<void(const std::map<TrackId, te::AudioTrack*>&)> callback) const {
    juce::ScopedLock lock(trackLock_);
    callback(trackMapping_);
}

// =============================================================================
// Metering Coordination
// =============================================================================

void TrackController::addMeterClient(TrackId trackId, te::LevelMeterPlugin* levelMeter) {
    if (!levelMeter)
        return;

    juce::ScopedLock lock(trackLock_);
    auto [it, inserted] = meterClients_.try_emplace(trackId);
    levelMeter->measurer.addClient(it->second);
}

void TrackController::removeMeterClient(TrackId trackId, te::LevelMeterPlugin* levelMeter) {
    juce::ScopedLock lock(trackLock_);
    auto it = meterClients_.find(trackId);
    if (it != meterClients_.end()) {
        if (levelMeter) {
            levelMeter->measurer.removeClient(it->second);
        }
        meterClients_.erase(it);
    }
}

void TrackController::withMeterClients(
    std::function<void(const std::map<TrackId, te::LevelMeasurer::Client>&)> callback) const {
    juce::ScopedLock lock(trackLock_);
    callback(meterClients_);
}

}  // namespace magda
