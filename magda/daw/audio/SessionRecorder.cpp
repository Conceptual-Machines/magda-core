#include "SessionRecorder.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include <iostream>

#include "../core/ClipCommands.hpp"
#include "../core/UndoManager.hpp"

namespace magda {

namespace te = tracktion;

SessionRecorder::SessionRecorder(te::Edit& edit) : edit_(edit) {
    ClipManager::getInstance().addListener(this);
}

SessionRecorder::~SessionRecorder() {
    ClipManager::getInstance().removeListener(this);
}

void SessionRecorder::setRecordingActive(bool active) {
    if (active == recordingActive_)
        return;

    if (active) {
        // Starting recording: snapshot arrangement clips and clear state
        auto& clipManager = ClipManager::getInstance();
        arrangementSnapshotBeforeRecord_ = clipManager.getArrangementClips();
        activeRecordings_.clear();
        createdArrangementClipIds_.clear();
        recordingActive_ = true;
        std::cout << "SessionRecorder: recording active" << std::endl;
    } else {
        // Stopping recording: finalize all active recordings and push undo command
        recordingActive_ = false;
        commitAllRecordings();
        std::cout << "SessionRecorder: recording stopped" << std::endl;
    }
}

bool SessionRecorder::isRecordingActive() const {
    return recordingActive_;
}

void SessionRecorder::clipPlaybackStateChanged(ClipId clipId) {
    if (!recordingActive_)
        return;

    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId);
    if (!clip || clip->view != ClipView::Session)
        return;

    double currentTime = edit_.getTransport().position.get().inSeconds();

    if (clip->isPlaying) {
        // A session clip started playing — finalize any existing recording on the same track
        for (auto it = activeRecordings_.begin(); it != activeRecordings_.end();) {
            if (it->second.trackId == clip->trackId) {
                finalizeRecording(it->second, currentTime);
                it = activeRecordings_.erase(it);
            } else {
                ++it;
            }
        }

        // Start a new active recording
        ActiveRecording rec;
        rec.sessionClipId = clipId;
        rec.trackId = clip->trackId;
        rec.arrangementStartTime = currentTime;
        activeRecordings_[clipId] = rec;

        std::cout << "SessionRecorder: started recording clip " << clipId << " on track "
                  << clip->trackId << " at " << currentTime << "s" << std::endl;
    } else if (!clip->isPlaying && !clip->isQueued) {
        // Clip stopped — finalize its recording
        auto it = activeRecordings_.find(clipId);
        if (it != activeRecordings_.end()) {
            finalizeRecording(it->second, currentTime);
            activeRecordings_.erase(it);
        }
    }
}

void SessionRecorder::finalizeRecording(const ActiveRecording& rec, double stopTime) {
    auto& clipManager = ClipManager::getInstance();
    const auto* sessionClip = clipManager.getClip(rec.sessionClipId);
    if (!sessionClip)
        return;

    double duration = stopTime - rec.arrangementStartTime;
    if (duration <= 0.001)
        return;  // Too short, skip

    // Create arrangement clip with the session clip's content
    ClipId newClipId = INVALID_CLIP_ID;

    if (sessionClip->type == ClipType::Audio) {
        if (sessionClip->audioFilePath.isNotEmpty()) {
            newClipId =
                clipManager.createAudioClip(rec.trackId, rec.arrangementStartTime, duration,
                                            sessionClip->audioFilePath, ClipView::Arrangement);
        }
    } else {
        newClipId = clipManager.createMidiClip(rec.trackId, rec.arrangementStartTime, duration,
                                               ClipView::Arrangement);
    }

    if (newClipId == INVALID_CLIP_ID)
        return;

    auto* newClip = clipManager.getClip(newClipId);
    if (!newClip)
        return;

    // Copy properties from session clip
    newClip->name = sessionClip->name;
    newClip->colour = sessionClip->colour;

    if (sessionClip->type == ClipType::Audio) {
        newClip->offset = sessionClip->offset;
        newClip->loopStart = sessionClip->loopStart;
        newClip->loopLength = sessionClip->loopLength;
        newClip->speedRatio = sessionClip->speedRatio;

        // For looping audio: enable loop if duration exceeds one pass
        double onePassDuration = sessionClip->length;
        if (sessionClip->loopEnabled && duration > onePassDuration) {
            newClip->loopEnabled = true;
        }
    } else if (sessionClip->type == ClipType::MIDI) {
        // For MIDI: tile notes across the played duration if looping
        double clipLengthBeats = sessionClip->lengthBeats;
        if (clipLengthBeats <= 0.0)
            clipLengthBeats = 4.0;

        // Get tempo for beat<->time conversion
        auto& tempoSeq = edit_.tempoSequence;
        auto beatPos =
            tempoSeq.timeToBeats(te::TimePosition::fromSeconds(rec.arrangementStartTime));
        auto endBeatPos = tempoSeq.timeToBeats(te::TimePosition::fromSeconds(stopTime));
        double totalBeats = endBeatPos.inBeats() - beatPos.inBeats();

        if (sessionClip->loopEnabled && totalBeats > clipLengthBeats) {
            // Tile notes across the duration
            int numPasses = static_cast<int>(std::ceil(totalBeats / clipLengthBeats));
            for (int pass = 0; pass < numPasses; ++pass) {
                double passOffset = pass * clipLengthBeats;
                for (const auto& note : sessionClip->midiNotes) {
                    MidiNote tiled = note;
                    tiled.startBeat += passOffset;
                    if (tiled.startBeat < totalBeats) {
                        // Trim note if it extends past the end
                        if (tiled.startBeat + tiled.lengthBeats > totalBeats) {
                            tiled.lengthBeats = totalBeats - tiled.startBeat;
                        }
                        newClip->midiNotes.push_back(tiled);
                    }
                }
            }
        } else {
            // Single pass, just copy notes (trim to duration)
            for (const auto& note : sessionClip->midiNotes) {
                if (note.startBeat < totalBeats) {
                    MidiNote trimmed = note;
                    if (trimmed.startBeat + trimmed.lengthBeats > totalBeats) {
                        trimmed.lengthBeats = totalBeats - trimmed.startBeat;
                    }
                    newClip->midiNotes.push_back(trimmed);
                }
            }
        }
    }

    clipManager.forceNotifyClipPropertyChanged(newClipId);
    createdArrangementClipIds_.push_back(newClipId);

    std::cout << "SessionRecorder: finalized clip " << rec.sessionClipId << " -> arrangement clip "
              << newClipId << " [" << rec.arrangementStartTime << "s - " << stopTime << "s]"
              << std::endl;
}

void SessionRecorder::commitAllRecordings() {
    double currentTime = edit_.getTransport().position.get().inSeconds();

    // Finalize any still-active recordings
    for (const auto& [clipId, rec] : activeRecordings_) {
        finalizeRecording(rec, currentTime);
    }
    activeRecordings_.clear();

    // Push undo command if any clips were created
    if (!createdArrangementClipIds_.empty()) {
        auto cmd =
            std::make_unique<RecordSessionToArrangementCommand>(arrangementSnapshotBeforeRecord_);
        UndoManager::getInstance().executeCommand(std::move(cmd));
        std::cout << "SessionRecorder: committed " << createdArrangementClipIds_.size()
                  << " arrangement clip(s) to undo stack" << std::endl;
    }

    arrangementSnapshotBeforeRecord_.clear();
    createdArrangementClipIds_.clear();
}

}  // namespace magda
