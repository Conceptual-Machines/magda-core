#include "TracktionEngineWrapper.hpp"

#include <iostream>

#include "../audio/AudioBridge.hpp"
#include "../core/ClipManager.hpp"

namespace magda {

// =============================================================================
// Recording Callbacks
// =============================================================================

void TracktionEngineWrapper::recordingAboutToStart(tracktion::InputDeviceInstance& instance,
                                                   tracktion::EditItemID targetID) {
    DBG("recordingAboutToStart: device='"
        << instance.owner.getName() << "' targetID=" << targetID.getRawID()
        << " instance.isRecording()=" << (int)instance.isRecording());

    // Store recording start time per track (first device wins)
    if (audioBridge_) {
        TrackId trackId = audioBridge_->getTrackIdForTeTrack(targetID);
        if (trackId != INVALID_TRACK_ID && recordingStartTimes_.count(trackId) == 0) {
            double startTime = getCurrentPosition();
            recordingStartTimes_[trackId] = startTime;
            DBG("  -> stored recording start time " << startTime << " for track " << trackId);

            // Create recording preview (no ClipManager clip — paint-only overlay)
            if (recordingPreviews_.count(trackId) == 0) {
                RecordingPreview preview;
                preview.trackId = trackId;
                preview.startTime = startTime;
                preview.currentLength = 0.0;
                recordingPreviews_[trackId] = std::move(preview);
            }
        }
    }
}

void TracktionEngineWrapper::recordingFinished(
    tracktion::InputDeviceInstance& instance, tracktion::EditItemID targetID,
    const juce::ReferenceCountedArray<tracktion::Clip>& recordedClips) {
    bool isPhysical = dynamic_cast<tracktion::PhysicalMidiInputDevice*>(&instance.owner) != nullptr;
    bool isVirtual = dynamic_cast<tracktion::VirtualMidiInputDevice*>(&instance.owner) != nullptr;
    DBG("recordingFinished: device='"
        << instance.owner.getName() << "' " << recordedClips.size() << " clips"
        << " targetID=" << (int)targetID.getRawID() << " physical=" << (int)isPhysical
        << " virtual=" << (int)isVirtual << " enabled=" << (int)instance.owner.isEnabled());
    if (!audioBridge_)
        return;

    TrackId trackId = audioBridge_->getTrackIdForTeTrack(targetID);

    for (auto* clip : recordedClips) {
        auto* midiClip = dynamic_cast<tracktion::MidiClip*>(clip);
        if (!midiClip)
            continue;

        if (trackId == INVALID_TRACK_ID) {
            if (auto* teTrack = dynamic_cast<tracktion::AudioTrack*>(midiClip->getTrack()))
                trackId = audioBridge_->getTrackIdForTeTrack(teTrack->itemID);
        }

        if (trackId == INVALID_TRACK_ID)
            continue;

        // One clip per track — skip if already processed by another device
        if (activeRecordingClips_.count(trackId) > 0) {
            // Merge data into existing clip
            ClipId clipId = activeRecordingClips_[trackId];
            auto& clipManager = ClipManager::getInstance();
            auto* clipInfo = clipManager.getClip(clipId);
            if (!clipInfo) {
                midiClip->removeFromParent();
                continue;
            }

            // Extract notes before removing the TE clip
            auto& midiList = midiClip->getSequence();
            for (auto* note : midiList.getNotes()) {
                if (!note)
                    continue;
                MidiNote mn;
                mn.noteNumber = note->getNoteNumber();
                mn.velocity = note->getVelocity();
                mn.startBeat = note->getStartBeat().inBeats();
                mn.lengthBeats = note->getLengthBeats().inBeats();
                clipInfo->midiNotes.push_back(mn);
            }

            DBG("  merged " << (int)midiList.getNotes().size() << " notes from device '"
                            << instance.owner.getName() << "' into clip " << clipId);

            midiClip->removeFromParent();

            // One sync after merging all notes
            if (audioBridge_)
                audioBridge_->syncClipToEngine(clipId);

            continue;
        }

        // First device for this track — create the MAGDA clip
        double startSeconds = midiClip->getPosition().getStart().inSeconds();
        double lengthSeconds = midiClip->getPosition().getLength().inSeconds();
        if (lengthSeconds <= 0.0) {
            midiClip->removeFromParent();
            continue;
        }

        // Extract ALL MIDI data from the TE recording clip BEFORE creating the MAGDA clip.
        // createMidiClip() triggers syncClipToEngine() which calls insertMIDIClip() on the
        // same track — this can invalidate the original recording clip's sequence data.
        std::vector<MidiNote> recordedNotes;
        std::vector<MidiCCData> recordedCC;
        std::vector<MidiPitchBendData> recordedPB;

        auto& midiList = midiClip->getSequence();
        for (auto* note : midiList.getNotes()) {
            if (!note)
                continue;
            MidiNote mn;
            mn.noteNumber = note->getNoteNumber();
            mn.velocity = note->getVelocity();
            mn.startBeat = note->getStartBeat().inBeats();
            mn.lengthBeats = note->getLengthBeats().inBeats();
            recordedNotes.push_back(mn);
        }

        for (auto* ce : midiList.getControllerEvents()) {
            if (!ce)
                continue;
            int eventType = ce->getType();
            if (eventType == tracktion::MidiControllerEvent::pitchWheelType) {
                MidiPitchBendData pb;
                pb.value = ce->getControllerValue();
                pb.beatPosition = ce->getBeatPosition().inBeats();
                recordedPB.push_back(pb);
            } else if (eventType < 128) {
                MidiCCData cc;
                cc.controller = eventType;
                cc.value = ce->getControllerValue();
                cc.beatPosition = ce->getBeatPosition().inBeats();
                recordedCC.push_back(cc);
            }
        }

        DBG("  extracted " << (int)recordedNotes.size() << " notes, " << (int)recordedCC.size()
                           << " CC, " << (int)recordedPB.size() << " pitchbend from TE clip");

        // Remove TE's recording clip BEFORE creating MAGDA clip to avoid
        // two clips overlapping on the same time range
        midiClip->removeFromParent();

        // Create the MAGDA clip (triggers syncClipToEngine with 0 notes — that's fine)
        auto& clipManager = ClipManager::getInstance();
        ClipId clipId =
            clipManager.createMidiClip(trackId, startSeconds, lengthSeconds, ClipView::Arrangement);
        activeRecordingClips_[trackId] = clipId;

        DBG("  created clip " << clipId << " on track " << trackId << " start=" << startSeconds
                              << " len=" << lengthSeconds);

        // Populate the MAGDA clip directly (bypass per-note notifications)
        auto* clipInfo = clipManager.getClip(clipId);
        if (clipInfo) {
            clipInfo->midiNotes = std::move(recordedNotes);
            clipInfo->midiCCData = std::move(recordedCC);
            clipInfo->midiPitchBendData = std::move(recordedPB);

            DBG("  populated clip with " << (int)clipInfo->midiNotes.size() << " notes");
        }

        // One final sync to push all MIDI data to the TE clip
        if (audioBridge_)
            audioBridge_->syncClipToEngine(clipId);
    }

    // Clear recording preview for this track — the real clip is now visible
    recordingPreviews_.erase(trackId);

    // Reset synths to prevent stuck notes
    if (trackId != INVALID_TRACK_ID) {
        audioBridge_->resetSynthsOnTrack(trackId);
    }
}

void TracktionEngineWrapper::drainRecordingNoteQueue() {
    double tempo = getTempo();
    double beatsPerSecond = tempo / 60.0;

    int eventsPopped = 0;
    RecordingNoteEvent evt;
    while (recordingNoteQueue_.pop(evt)) {
        eventsPopped++;
        TrackId trackId = evt.trackId;
        auto it = recordingPreviews_.find(trackId);
        if (it == recordingPreviews_.end())
            continue;

        auto& preview = it->second;

        if (evt.isNoteOn) {
            MidiNote mn;
            mn.noteNumber = evt.noteNumber;
            mn.velocity = evt.velocity;
            mn.startBeat = (evt.transportSeconds - preview.startTime) * beatsPerSecond;
            mn.lengthBeats = -1.0;  // Sentinel: note still held
            preview.notes.push_back(mn);
        } else {
            // Note-off: find matching open note (same noteNumber, lengthBeats < 0)
            for (int i = static_cast<int>(preview.notes.size()) - 1; i >= 0; --i) {
                auto& n = preview.notes[static_cast<size_t>(i)];
                if (n.noteNumber == evt.noteNumber && n.lengthBeats < 0.0) {
                    double endBeat = (evt.transportSeconds - preview.startTime) * beatsPerSecond;
                    n.lengthBeats = endBeat - n.startBeat;
                    if (n.lengthBeats < 0.01)
                        n.lengthBeats = 0.01;
                    break;
                }
            }
        }
    }

    if (eventsPopped > 0) {
        DBG("RecPreview::drain: popped=" << eventsPopped);
    }

    // Grow each preview's currentLength to match playhead
    double currentPos = getCurrentPosition();
    for (auto& [trackId, preview] : recordingPreviews_) {
        double newLength = currentPos - preview.startTime;
        if (newLength > preview.currentLength)
            preview.currentLength = newLength;
    }
}

}  // namespace magda
