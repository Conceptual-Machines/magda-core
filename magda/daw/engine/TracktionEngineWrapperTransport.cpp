#include "TracktionEngineWrapper.hpp"

#include <iostream>

#include "../audio/AudioBridge.hpp"
#include "../audio/SessionClipScheduler.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"

namespace magda {

CommandResponse TracktionEngineWrapper::processCommand(const Command& command) {
    const auto& type = command.getType();

    try {
        if (type == "play") {
            play();
            return CommandResponse(CommandResponse::Status::Success, "Playback started");
        } else if (type == "stop") {
            stop();
            return CommandResponse(CommandResponse::Status::Success, "Playback stopped");
        } else if (type == "createTrack") {
            // Simple parameter parsing - in a real implementation you'd parse JSON
            auto trackId = createMidiTrack("New Track");

            juce::DynamicObject::Ptr obj = new juce::DynamicObject();
            obj->setProperty("trackId", juce::String(trackId));
            juce::var responseData(obj.get());

            auto response = CommandResponse(CommandResponse::Status::Success, "Track created");
            response.setData(responseData);
            return response;
        } else {
            return CommandResponse(CommandResponse::Status::Error, "Unknown command");
        }
    } catch (const std::exception& e) {
        return CommandResponse(CommandResponse::Status::Error,
                               "Command execution failed: " + std::string(e.what()));
    }
}

// TransportInterface implementation
void TracktionEngineWrapper::play() {
    // Block playback while devices are loading to prevent audio glitches
    if (devicesLoading_) {
        std::cout << "Playback blocked - devices still loading" << std::endl;
        return;
    }

    if (currentEdit_) {
        auto& transport = currentEdit_->getTransport();

        // Detect stale audio device (e.g. CoreAudio daemon stuck after sleep)
        // Check multiple times to avoid false positives from momentary zero readings
        auto& jdm = engine_->getDeviceManager().deviceManager;
        auto* device = jdm.getCurrentAudioDevice();
        if (device && device->isPlaying() && jdm.getCpuUsage() == 0.0) {
            int zeroCount = 1;
            for (int i = 0; i < AUDIO_DEVICE_CHECK_RETRIES; ++i) {
                juce::Thread::sleep(AUDIO_DEVICE_CHECK_SLEEP_MS);
                if (jdm.getCpuUsage() == 0.0)
                    ++zeroCount;
            }
            if (zeroCount >= AUDIO_DEVICE_CHECK_THRESHOLD) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Audio Device Not Responding",
                    "The audio device '" + device->getName() +
                        "' is not processing audio.\n\n"
                        "Try disconnecting and reconnecting your audio interface, "
                        "or restarting the audio driver.",
                    "OK");
            }
        }

        transport.play(false);
        std::cout << "Playback started" << std::endl;
    }
}

void TracktionEngineWrapper::stop() {
    if (currentEdit_) {
        currentEdit_->getTransport().stop(false, false);
        std::cout << "Playback stopped" << std::endl;
    }
}

void TracktionEngineWrapper::pause() {
    stop();  // Tracktion doesn't distinguish between stop and pause
}

void TracktionEngineWrapper::record() {
    // Block recording while devices are loading
    if (devicesLoading_) {
        DBG("TracktionEngineWrapper::record() - blocked, devices still loading");
        return;
    }

    if (currentEdit_) {
        // Dump all input device instances and their record-enabled state
        if (auto* ctx = currentEdit_->getCurrentPlaybackContext()) {
            DBG("TracktionEngineWrapper::record() - input devices before record:");
            for (auto* input : ctx->getAllInputs()) {
                auto& dev = input->owner;
                bool isMidi = dynamic_cast<tracktion::MidiInputDevice*>(&dev) != nullptr;
                DBG("  device='" << dev.getName() << "' type=" << (isMidi ? "MIDI" : "Audio")
                                 << " enabled=" << (dev.isEnabled() ? "Y" : "N")
                                 << " destinations=" << (int)input->destinations.size());
                for (auto* dest : input->destinations) {
                    DBG("    dest targetID=" << dest->targetID.getRawID() << " recordEnabled="
                                             << (dest->recordEnabled ? "Y" : "N"));
                }
            }
        } else {
            DBG("TracktionEngineWrapper::record() - NO playback context!");
        }

        DBG("TracktionEngineWrapper::record() - calling transport.record(false)");
        currentEdit_->getTransport().record(false);
        DBG("TracktionEngineWrapper::record() - isRecording=" << (int)isRecording());

        // Verify recording state on all input instances after record() returns
        if (auto* ctx = currentEdit_->getCurrentPlaybackContext()) {
            DBG("TracktionEngineWrapper::record() - post-record instance states:");
            for (auto* input : ctx->getAllInputs()) {
                if (dynamic_cast<tracktion::MidiInputDevice*>(&input->owner)) {
                    DBG("  device='" << input->owner.getName()
                                     << "' isRecording()=" << (int)input->isRecording()
                                     << " isRecordingActive()=" << (int)input->isRecordingActive());
                }
            }
        }
    }
}

void TracktionEngineWrapper::locate(double position_seconds) {
    if (currentEdit_) {
        currentEdit_->getTransport().setPosition(
            tracktion::TimePosition::fromSeconds(position_seconds));
    }
}

void TracktionEngineWrapper::locateMusical(int bar, int beat, int tick) {
    // Convert musical position to time
    if (currentEdit_) {
        auto& tempoSequence = currentEdit_->tempoSequence;
        auto beatPosition =
            tracktion::BeatPosition::fromBeats(bar * 4.0 + beat - 1.0 + tick / 1000.0);
        auto timePosition = tempoSequence.beatsToTime(beatPosition);
        currentEdit_->getTransport().setPosition(timePosition);
    }
}

double TracktionEngineWrapper::getCurrentPosition() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().position.get().inSeconds();
    }
    return 0.0;
}

void TracktionEngineWrapper::getCurrentMusicalPosition(int& bar, int& beat, int& tick) const {
    if (currentEdit_) {
        auto position = tracktion::TimePosition::fromSeconds(getCurrentPosition());
        auto& tempoSequence = currentEdit_->tempoSequence;
        auto beatPosition = tempoSequence.timeToBeats(position);
        auto beats = beatPosition.inBeats();

        bar = static_cast<int>(beats / 4.0) + 1;
        beat = static_cast<int>(beats) % 4 + 1;
        tick = static_cast<int>((beats - static_cast<int>(beats)) * 1000);
    }
}

bool TracktionEngineWrapper::isPlaying() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().isPlaying();
    }
    return false;
}

bool TracktionEngineWrapper::isRecording() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().isRecording();
    }
    return false;
}

void TracktionEngineWrapper::setTempo(double bpm) {
    if (currentEdit_) {
        auto& tempoSeq = currentEdit_->tempoSequence;
        if (tempoSeq.getNumTempos() > 0) {
            auto tempo = tempoSeq.getTempo(0);
            if (tempo) {
                tempo->setBpm(bpm);
                std::cout << "Set tempo: " << bpm << " BPM" << std::endl;
            }
        }
    }
}

double TracktionEngineWrapper::getTempo() const {
    if (currentEdit_) {
        auto timePos = tracktion::TimePosition::fromSeconds(0.0);
        return currentEdit_->tempoSequence.getTempoAt(timePos).getBpm();
    }
    return 120.0;
}

void TracktionEngineWrapper::setTimeSignature(int numerator, int denominator) {
    if (currentEdit_) {
        // Time signature handling in Tracktion Engine - simplified for now
        std::cout << "Set time signature: " << numerator << "/" << denominator << std::endl;
    }
}

void TracktionEngineWrapper::getTimeSignature(int& numerator, int& denominator) const {
    if (currentEdit_) {
        // Time signature handling in Tracktion Engine - simplified for now
        numerator = 4;
        denominator = 4;
    } else {
        numerator = 4;
        denominator = 4;
    }
}

void TracktionEngineWrapper::setLooping(bool enabled) {
    if (currentEdit_) {
        currentEdit_->getTransport().looping = enabled;
    }
}

void TracktionEngineWrapper::setLoopRegion(double start_seconds, double end_seconds) {
    if (currentEdit_) {
        auto startPos = tracktion::TimePosition::fromSeconds(start_seconds);
        auto endPos = tracktion::TimePosition::fromSeconds(end_seconds);
        currentEdit_->getTransport().setLoopRange(tracktion::TimeRange(startPos, endPos));
    }
}

bool TracktionEngineWrapper::isLooping() const {
    if (currentEdit_) {
        return currentEdit_->getTransport().looping;
    }
    return false;
}

bool TracktionEngineWrapper::justStarted() const {
    return justStarted_;
}

bool TracktionEngineWrapper::justLooped() const {
    return justLooped_;
}

void TracktionEngineWrapper::updateTriggerState() {
    // Reset flags at start of each frame
    justStarted_ = false;
    justLooped_ = false;

    bool currentlyPlaying = isPlaying();
    double currentPosition = getCurrentPosition();

    // Update transport position for MIDI recording preview
    transportPositionForMidi_.store(currentPosition, std::memory_order_relaxed);

    // Detect play start (was not playing, now playing)
    if (currentlyPlaying && !wasPlaying_) {
        justStarted_ = true;
    }

    // Detect loop (position jumped backward while playing and looping)
    if (currentlyPlaying && isLooping() && currentPosition < lastPosition_) {
        // Position went backward - likely a loop
        // Add a threshold to avoid false positives from small position jitter
        if (lastPosition_ - currentPosition > 0.1) {  // More than 100ms backward
            justLooped_ = true;
        }
    }

    // Update state for next frame
    wasPlaying_ = currentlyPlaying;
    lastPosition_ = currentPosition;

    // Update AudioBridge with transport state for trigger sync
    if (audioBridge_) {
        audioBridge_->updateTransportState(currentlyPlaying, justStarted_, justLooped_);
    }

    // Drain recording note queue and grow preview clips
    if (!recordingPreviews_.empty()) {
        drainRecordingNoteQueue();
    }
}

// Metronome/click track methods
void TracktionEngineWrapper::setMetronomeEnabled(bool enabled) {
    if (currentEdit_) {
        currentEdit_->clickTrackEnabled = enabled;
        std::cout << "Metronome " << (enabled ? "enabled" : "disabled") << std::endl;
    }
}

bool TracktionEngineWrapper::isMetronomeEnabled() const {
    if (currentEdit_) {
        return currentEdit_->clickTrackEnabled;
    }
    return false;
}

// ===== AudioEngineListener Implementation =====
// These methods are called by TimelineController when UI state changes

void TracktionEngineWrapper::onTransportPlay(double position) {
    // If a session clip is selected, trigger it via the clip launcher
    auto& cm = ClipManager::getInstance();
    ClipId selectedClip = cm.getSelectedClip();
    if (selectedClip != INVALID_CLIP_ID) {
        const auto* clip = cm.getClip(selectedClip);
        if (clip && clip->view == ClipView::Session && !clip->isPlaying && !clip->isQueued) {
            cm.triggerClip(selectedClip);
            return;  // SessionClipScheduler will start transport
        }
    }

    locate(position);
    play();
}

void TracktionEngineWrapper::onTransportStop(double returnPosition) {
    // Stop any playing session clips
    if (sessionScheduler_) {
        sessionScheduler_->deactivateAllSessionClips();
    }

    // Capture current position before stopping (this is the recording end time)
    double stopPosition = getCurrentPosition();

    // transport.stop() triggers recordingFinished() synchronously per device,
    // which populates activeRecordingClips_ for cross-device dedup.
    stop();

    // For any track that was recording but got 0 clips from TE (blank MIDI recording),
    // create an empty MIDI clip ourselves — but only if the track has MIDI input configured.
    if (!recordingStartTimes_.empty()) {
        auto& clipManager = ClipManager::getInstance();
        for (auto& [trackId, startTime] : recordingStartTimes_) {
            // Reset synths on every recorded track (fixes stuck notes)
            if (audioBridge_) {
                audioBridge_->resetSynthsOnTrack(trackId);
            }

            // Skip if TE already created a clip for this track
            if (activeRecordingClips_.count(trackId) > 0) {
                continue;
            }

            // Only create empty MIDI clip if the track actually has MIDI input configured.
            // Audio-only tracks should not get a blank MIDI clip.
            const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
            if (!trackInfo || trackInfo->midiInputDevice.isEmpty()) {
                continue;
            }

            double length = stopPosition - startTime;
            if (length > 0.01) {
                ClipId clipId =
                    clipManager.createMidiClip(trackId, startTime, length, ClipView::Arrangement);
                DBG("Created empty recording clip " << clipId << " on track " << trackId
                                                    << " start=" << startTime << " len=" << length);
            }
        }
    }

    // Final drain and clear recording previews
    drainRecordingNoteQueue();
    recordingPreviews_.clear();
    recordingNoteQueue_.clear();

    // Clear dedup maps
    activeRecordingClips_.clear();
    recordingStartTimes_.clear();

    locate(returnPosition);
}

void TracktionEngineWrapper::onTransportPause() {
    pause();
}

void TracktionEngineWrapper::onTransportRecord(double position) {
    locate(position);
    record();
}

void TracktionEngineWrapper::onTransportStopRecording() {
    if (!currentEdit_)
        return;

    // Capture stop position before TE processes the stop
    double stopPosition = getCurrentPosition();

    // TE's stopRecording stops recording but keeps playback going.
    // This triggers recordingFinished() synchronously per device.
    currentEdit_->getTransport().stopRecording(false);

    // Create clips for tracks that got 0 clips from TE (blank MIDI recording)
    if (!recordingStartTimes_.empty()) {
        auto& clipManager = ClipManager::getInstance();
        for (auto& [trackId, startTime] : recordingStartTimes_) {
            if (audioBridge_)
                audioBridge_->resetSynthsOnTrack(trackId);

            if (activeRecordingClips_.count(trackId) > 0)
                continue;

            // Only create empty MIDI clip if the track actually has MIDI input configured.
            const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
            if (!trackInfo || trackInfo->midiInputDevice.isEmpty())
                continue;

            double length = stopPosition - startTime;
            if (length > 0.01) {
                clipManager.createMidiClip(trackId, startTime, length, ClipView::Arrangement);
            }
        }
    }

    // Final drain and clear recording previews
    drainRecordingNoteQueue();
    recordingPreviews_.clear();
    recordingNoteQueue_.clear();

    activeRecordingClips_.clear();
    recordingStartTimes_.clear();
}

void TracktionEngineWrapper::onEditPositionChanged(double position) {
    // Only seek if not currently playing
    if (!isPlaying()) {
        locate(position);
    }
}

void TracktionEngineWrapper::onTempoChanged(double bpm) {
    setTempo(bpm);
}

void TracktionEngineWrapper::onTimeSignatureChanged(int numerator, int denominator) {
    setTimeSignature(numerator, denominator);
}

void TracktionEngineWrapper::onLoopRegionChanged(double startTime, double endTime, bool enabled) {
    setLoopRegion(startTime, endTime);
    setLooping(enabled);
}

void TracktionEngineWrapper::onLoopEnabledChanged(bool enabled) {
    setLooping(enabled);
}

}  // namespace magda
