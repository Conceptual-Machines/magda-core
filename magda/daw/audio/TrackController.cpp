#include "TrackController.hpp"

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

        DBG("TrackController: Created Tracktion AudioTrack for MAGDA track "
            << trackId << ": " << name << " (routed to master)");
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
        DBG("TrackController: Removed Tracktion AudioTrack for MAGDA track " << trackId);
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

    if (destination.isEmpty()) {
        // Disable output by routing to nothing
        if (!track->getOutput().getOutputDevice(false))
            return;  // Already has no output
        track->getOutput().setOutputToDeviceID({});
    } else if (destination == "master") {
        // Skip if already routed to default — avoids unnecessary graph rebuild
        if (track->getOutput().usesDefaultAudioOut())
            return;
        track->getOutput().setOutputToDefaultDevice(false);  // false = audio (not MIDI)
    } else if (destination.startsWith("track:")) {
        // Route to another track (group or aux)
        TrackId targetId = destination.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        auto* targetTrack = getAudioTrack(targetId);
        if (targetTrack) {
            // Clear first to ensure TE detects the change even when the
            // positional "track N" string happens to match the previous value
            // (e.g., after tracks are added/removed and positions shift).
            track->getOutput().setOutputToDeviceID({});
            track->getOutput().setOutputToTrack(targetTrack);
        } else {
            DBG("TrackController::setTrackAudioOutput - target track not found: trackId="
                << trackId << " targetId=" << targetId << " — falling back to master");
            track->getOutput().setOutputToDefaultDevice(false);
        }
    } else {
        // Route to specific output device
        track->getOutput().setOutputToDeviceID(destination);
    }
}

juce::String TrackController::getTrackAudioOutput(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return {};
    }

    auto& output = track->getOutput();
    if (output.usesDefaultAudioOut()) {
        return "master";  // Consistent with "master" keyword in setTrackAudioOutput
    }

    // Check if routed to another track
    if (auto* destTrack = output.getDestinationTrack()) {
        // Find the MAGDA TrackId for this TE track
        juce::ScopedLock lock(trackLock_);
        for (const auto& [magdaId, teTrack] : trackMapping_) {
            if (teTrack == destTrack) {
                return "track:" + juce::String(magdaId);
            }
        }
    }

    // Return the output device ID for round-trip consistency
    return output.getOutputName();
}

void TrackController::setTrackMidiOutput(TrackId trackId, const juce::String& deviceId) {
    // NOTE: TE has a single TrackOutput per track (shared audio+MIDI).
    // We cannot use TrackOutput for independent MIDI routing — it would
    // replace the audio output. MIDI routing will be handled via aux sends.
    juce::ignoreUnused(trackId, deviceId);
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
    } else if (deviceId.startsWith("track:")) {
        // Route another track's audio output as input (resampling)
        TrackId sourceTrackId =
            deviceId.fromFirstOccurrenceOf("track:", false, false).getIntValue();
        auto* sourceTrack = getAudioTrack(sourceTrackId);
        if (sourceTrack) {
            auto* dest =
                te::assignTrackAsInput(*track, *sourceTrack, te::InputDevice::trackWaveDevice);
            if (dest) {
                dest->recordEnabled = false;  // Arming happens separately
                DBG("  -> Assigned track " << sourceTrackId << " as audio input");
            } else {
                DBG("  -> Warning: assignTrackAsInput returned null");
            }
        } else {
            DBG("  -> Source track not found: " << sourceTrackId);
        }
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
                // Strip "stereo:" prefix if present — routing resolves to same device
                auto resolvedName = deviceId.startsWith("stereo:")
                                        ? deviceId.fromFirstOccurrenceOf("stereo:", false, false)
                                        : deviceId;
                // Find specific device by name and route it
                for (auto* inputDeviceInstance : allInputs) {
                    if (inputDeviceInstance->owner.getName() == resolvedName) {
                        auto result = inputDeviceInstance->setTarget(track->itemID, false, nullptr);
                        if (result.has_value()) {
                            (*result)->recordEnabled = false;
                            DBG("  -> Routed input '" << resolvedName << "' to track");
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
                    // Check if this is a track-as-input (resampling) device
                    if (inputDeviceInstance->owner.isTrackDevice() &&
                        inputDeviceInstance->owner.getDeviceType() ==
                            te::InputDevice::trackWaveDevice) {
                        // Find the source MAGDA TrackId for this track input device
                        juce::ScopedLock lock(trackLock_);
                        for (const auto& [magdaId, teTrack] : trackMapping_) {
                            if (&teTrack->getWaveInputDevice() == &inputDeviceInstance->owner) {
                                return "track:" + juce::String(magdaId);
                            }
                        }
                    }
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
