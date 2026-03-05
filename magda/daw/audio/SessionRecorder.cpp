#include "SessionRecorder.hpp"

#include <tracktion_engine/tracktion_engine.h>

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

void SessionRecorder::setArmed(bool armed) {
    if (armed == armed_)
        return;
    armed_ = armed;

    // When arming, pick up any session clips that are already playing.
    // clipPlaybackStateChanged only fires on transitions, so clips that
    // were already playing before arming would otherwise be missed.
    if (armed_ && getPlayState_) {
        auto& clipManager = ClipManager::getInstance();
        for (const auto& clip : clipManager.getSessionClips()) {
            auto state = getPlayState_(clip.id);
            if (state == SessionClipPlayState::Playing) {
                ensureSnapshotTaken();

                double launchTime = edit_.getTransport().position.get().inSeconds();
                if (getLaunchTime_) {
                    double precise = getLaunchTime_(clip.trackId);
                    if (precise > 0.0)
                        launchTime = precise;
                }

                ActiveRecording rec;
                rec.sessionClipId = clip.id;
                rec.trackId = clip.trackId;
                rec.arrangementStartTime = launchTime;
                activeRecordings_[clip.id] = rec;

                if (recordingPreviews_) {
                    RecordingPreview preview;
                    preview.trackId = clip.trackId;
                    preview.startTime = launchTime;
                    preview.currentLength = 0.0;
                    preview.isAudioRecording = (clip.type == ClipType::Audio);
                    (*recordingPreviews_)[clip.trackId] = preview;
                }
            }
        }
    }
}

void SessionRecorder::updatePreviews() {
    if (!armed_ || !recordingPreviews_ || activeRecordings_.empty())
        return;

    auto& clipManager = ClipManager::getInstance();
    auto& tempoSeq = edit_.tempoSequence;
    double currentTime = edit_.getTransport().position.get().inSeconds();

    for (const auto& [clipId, rec] : activeRecordings_) {
        auto it = recordingPreviews_->find(rec.trackId);
        if (it == recordingPreviews_->end())
            continue;

        auto& preview = it->second;
        preview.currentLength = currentTime - rec.arrangementStartTime;

        // Populate preview notes from the session clip's MIDI data
        const auto* sessionClip = clipManager.getClip(rec.sessionClipId);
        if (!sessionClip || sessionClip->type != ClipType::MIDI || sessionClip->midiNotes.empty())
            continue;

        double clipLengthBeats = sessionClip->lengthBeats;
        if (clipLengthBeats <= 0.0)
            clipLengthBeats = 4.0;

        auto startBeatPos =
            tempoSeq.toBeats(te::TimePosition::fromSeconds(rec.arrangementStartTime));
        auto endBeatPos = tempoSeq.toBeats(te::TimePosition::fromSeconds(currentTime));
        double totalBeats = endBeatPos.inBeats() - startBeatPos.inBeats();
        if (totalBeats <= 0.0)
            continue;

        // Rebuild notes (tiled if looping) — cheap enough at ~30fps.
        // Note coordinates are in beats relative to the preview start,
        // matching the rendering code's expectation (currentLength * beatsPerSecond).
        preview.notes.clear();

        if (sessionClip->loopEnabled && totalBeats > clipLengthBeats) {
            int numPasses = static_cast<int>(std::ceil(totalBeats / clipLengthBeats));
            for (int pass = 0; pass < numPasses; ++pass) {
                double passOffset = pass * clipLengthBeats;
                for (const auto& note : sessionClip->midiNotes) {
                    double noteStart = note.startBeat + passOffset;
                    if (noteStart >= totalBeats)
                        break;
                    MidiNote tiled = note;
                    tiled.startBeat = noteStart;
                    preview.notes.push_back(tiled);
                }
            }
        } else {
            for (const auto& note : sessionClip->midiNotes) {
                if (note.startBeat >= totalBeats)
                    continue;
                preview.notes.push_back(note);
            }
        }
    }
}

void SessionRecorder::ensureSnapshotTaken() {
    if (!snapshotTaken_) {
        arrangementSnapshotBeforeRecord_ = ClipManager::getInstance().getArrangementClips();
        snapshotTaken_ = true;
    }
}

void SessionRecorder::clipPlaybackStateChanged(ClipId clipId) {
    if (!armed_)
        return;

    auto& clipManager = ClipManager::getInstance();
    const auto* clip = clipManager.getClip(clipId);
    if (!clip || clip->view != ClipView::Session)
        return;

    // Query the scheduler for the actual play state
    auto state = getPlayState_ ? getPlayState_(clipId) : SessionClipPlayState::Stopped;

    auto& transport = edit_.getTransport();
    double currentTime = transport.position.get().inSeconds();

    if (state == SessionClipPlayState::Playing) {
        ensureSnapshotTaken();

        // Use precise quantized launch time when available, fall back to transport position
        double launchTime = currentTime;
        if (getLaunchTime_) {
            double precise = getLaunchTime_(clip->trackId);
            if (precise > 0.0) {
                launchTime = precise;
            }
        }

        // Finalize any existing recording on the same track (clip replaced)
        for (auto it = activeRecordings_.begin(); it != activeRecordings_.end();) {
            if (it->second.trackId == clip->trackId) {
                finalizeRecording(it->second, launchTime);
                it = activeRecordings_.erase(it);
            } else {
                ++it;
            }
        }

        // Start a new active recording
        ActiveRecording rec;
        rec.sessionClipId = clipId;
        rec.trackId = clip->trackId;
        rec.arrangementStartTime = launchTime;
        activeRecordings_[clipId] = rec;

        // Create recording preview for real-time UI
        if (recordingPreviews_) {
            RecordingPreview preview;
            preview.trackId = clip->trackId;
            preview.startTime = launchTime;
            preview.currentLength = 0.0;
            preview.isAudioRecording = (clip->type == ClipType::Audio);
            (*recordingPreviews_)[clip->trackId] = preview;
        }
    } else if (state == SessionClipPlayState::Stopped) {
        // Clip stopped — finalize its recording
        auto it = activeRecordings_.find(clipId);
        if (it != activeRecordings_.end()) {
            if (recordingPreviews_)
                recordingPreviews_->erase(it->second.trackId);
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
        return;

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

        // Copy beat-mode / auto-tempo properties so the arrangement clip
        // stays in the same time-stretch mode as the session clip.
        newClip->sourceBPM = sessionClip->sourceBPM;
        newClip->sourceNumBeats = sessionClip->sourceNumBeats;
        newClip->timeStretchMode = sessionClip->timeStretchMode;
        newClip->loopStartBeats = sessionClip->loopStartBeats;
        newClip->loopLengthBeats = sessionClip->loopLengthBeats;

        // For autoTempo arrangement clips, startBeats and lengthBeats must
        // reflect the ARRANGEMENT position, not the session clip's values
        // (session clips have startBeats=0 since they live in slots).
        if (sessionClip->autoTempo) {
            auto& tempoSeq = edit_.tempoSequence;
            auto startBeatPos =
                tempoSeq.toBeats(te::TimePosition::fromSeconds(rec.arrangementStartTime));
            auto endBeatPos = tempoSeq.toBeats(te::TimePosition::fromSeconds(stopTime));
            newClip->startBeats = startBeatPos.inBeats();
            newClip->lengthBeats = endBeatPos.inBeats() - startBeatPos.inBeats();
            newClip->autoTempo = true;
        } else {
            newClip->lengthBeats = sessionClip->lengthBeats;
        }

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

        auto& tempoSeq = edit_.tempoSequence;
        auto beatPos =
            tempoSeq.timeToBeats(te::TimePosition::fromSeconds(rec.arrangementStartTime));
        auto endBeatPos = tempoSeq.timeToBeats(te::TimePosition::fromSeconds(stopTime));
        double totalBeats = endBeatPos.inBeats() - beatPos.inBeats();

        if (sessionClip->loopEnabled && totalBeats > clipLengthBeats) {
            int numPasses = static_cast<int>(std::ceil(totalBeats / clipLengthBeats));
            for (int pass = 0; pass < numPasses; ++pass) {
                double passOffset = pass * clipLengthBeats;
                for (const auto& note : sessionClip->midiNotes) {
                    MidiNote tiled = note;
                    tiled.startBeat += passOffset;
                    if (tiled.startBeat < totalBeats) {
                        if (tiled.startBeat + tiled.lengthBeats > totalBeats) {
                            tiled.lengthBeats = totalBeats - tiled.startBeat;
                        }
                        newClip->midiNotes.push_back(tiled);
                    }
                }
            }
        } else {
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
}

void SessionRecorder::commitIfNeeded() {
    if (activeRecordings_.empty() && createdArrangementClipIds_.empty())
        return;

    auto& transport = edit_.getTransport();
    double currentTime = transport.position.get().inSeconds();

    // Clear recording previews
    if (recordingPreviews_)
        recordingPreviews_->clear();

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

        // Rebuild the audio graph so new arrangement clips are audible immediately
        if (auto* ctx = edit_.getCurrentPlaybackContext())
            ctx->reallocate();
    }

    arrangementSnapshotBeforeRecord_.clear();
    createdArrangementClipIds_.clear();
    snapshotTaken_ = false;
}

}  // namespace magda
