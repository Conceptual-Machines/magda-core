#include "../audio/AudioBridge.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"
#include "../project/ProjectManager.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda {

void TracktionEngineWrapper::recordingAboutToStart(tracktion::InputDeviceInstance& instance,
                                                   tracktion::EditItemID targetID) {
    DBG("recordingAboutToStart: device='"
        << instance.owner.getName() << "' targetID=" << targetID.getRawID()
        << " instance.isRecording()=" << (int)instance.isRecording());

    // Store recording start time per track (first device wins) and ensure the
    // paint-only preview exists. TE can fire a phantom finished→aboutToStart
    // pair on every input instance after it reallocates the playback graph at
    // the top of a record session, so the preview add is NOT gated on
    // recordingStartTimes_ — it must survive (or re-appear after) those
    // graph-churn finishes.
    if (audioBridge_) {
        TrackId trackId = audioBridge_->getTrackIdForTeTrack(targetID);
        if (trackId != INVALID_TRACK_ID) {
            if (recordingStartTimes_.count(trackId) == 0) {
                recordingStartTimes_[trackId] = intendedRecordPosition_;
                DBG("  -> stored recording start time " << intendedRecordPosition_ << " for track "
                                                        << trackId);
            }

            if (recordingPreviews_.count(trackId) == 0) {
                RecordingPreview preview;
                preview.trackId = trackId;
                preview.startTime = recordingStartTimes_[trackId];
                preview.currentLength = 0.0;

                const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
                preview.isAudioRecording = trackInfo && !trackInfo->audioInputDevice.isEmpty();

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

    // Determine what input types this track is configured for.
    // This is the authoritative source — only create clips matching the track's input config.
    const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    bool hasAudioInput = trackInfo && !trackInfo->audioInputDevice.isEmpty();
    bool hasMidiInput = trackInfo && !trackInfo->midiInputDevice.isEmpty();

    DBG("  trackId=" << trackId << " hasAudioInput=" << (int)hasAudioInput
                     << " hasMidiInput=" << (int)hasMidiInput);

    for (auto* clip : recordedClips) {
        // Handle audio (wave) clips
        auto* audioClip = dynamic_cast<tracktion::WaveAudioClip*>(clip);
        if (audioClip) {
            if (!hasAudioInput) {
                DBG("  skipping audio clip — track has no audio input configured");
                audioClip->removeFromParent();
                continue;
            }
            juce::String audioFilePath = audioClip->getOriginalFile().getFullPathName();
            double startSeconds = audioClip->getPosition().getStart().inSeconds();
            double lengthSeconds = audioClip->getPosition().getLength().inSeconds();

            if (trackId == INVALID_TRACK_ID) {
                if (auto* teTrack = dynamic_cast<tracktion::AudioTrack*>(audioClip->getTrack()))
                    trackId = audioBridge_->getTrackIdForTeTrack(teTrack->itemID);
            }

            if (lengthSeconds <= 0.0 || audioFilePath.isEmpty() || trackId == INVALID_TRACK_ID) {
                audioClip->removeFromParent();
                continue;
            }

            // Remove TE's recording clip before creating MAGDA clip
            audioClip->removeFromParent();

            // Create MAGDA audio clip (triggers syncClipToEngine which re-creates in TE)
            auto& clipManager = ClipManager::getInstance();
            ClipId clipId = clipManager.createAudioClip(trackId, startSeconds, lengthSeconds,
                                                        audioFilePath, ClipView::Arrangement);

            // Set source interpretation BPM to the project tempo — we know the exact BPM the clip
            // was recorded at, so skip unreliable auto-detection in syncClipToEngine.
            if (auto* newClip = clipManager.getClip(clipId)) {
                double projectBPM = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
                if (projectBPM > 0.0) {
                    newClip->audio().interpretation.bpm = projectBPM;
                    newClip->audio().interpretation.totalBeats = lengthSeconds * projectBPM / 60.0;
                }
            }

            if (audioBridge_)
                audioBridge_->syncClipToEngine(clipId);

            DBG("  created audio clip " << clipId << " file=" << audioFilePath);
            continue;
        }

        // Handle MIDI clips.
        //
        // TE fires recordingFinished once per input device. With mergeRecordings=
        // true (TE default), the FIRST device's applyRecording creates a MidiClip
        // and every subsequent device's applyRecording merges its events INTO
        // that clip via track->mergeInMidiSequence — returning 0 clips to the
        // listener. If we extract + remove eagerly on the first callback, later
        // devices (e.g. QWERTY) have nothing to merge into and their notes are
        // silently dropped. So: stash the first clip, drop any later duplicate
        // clips on the same track, and extract the merged state asynchronously
        // once all synchronous callbacks have settled.
        auto* midiClip = dynamic_cast<tracktion::MidiClip*>(clip);
        if (!midiClip)
            continue;

        if (!hasMidiInput) {
            DBG("  skipping MIDI clip — track has no MIDI input configured");
            midiClip->removeFromParent();
            continue;
        }

        if (trackId == INVALID_TRACK_ID) {
            if (auto* teTrack = dynamic_cast<tracktion::AudioTrack*>(midiClip->getTrack()))
                trackId = audioBridge_->getTrackIdForTeTrack(teTrack->itemID);
        }

        if (trackId == INVALID_TRACK_ID)
            continue;

        if (midiClip->getPosition().getLength().inSeconds() <= 0.0) {
            midiClip->removeFromParent();
            continue;
        }

        auto pendingIt = pendingMidiRecordings_.find(trackId);
        if (pendingIt == pendingMidiRecordings_.end()) {
            pendingMidiRecordings_[trackId] = midiClip;
            DBG("  stashed TE midi clip for deferred finalize on track " << trackId);
        } else if (pendingIt->second.get() != midiClip) {
            // A second device (mergeRecordings must be false for this device)
            // produced its own overlapping clip. Drop it — we only commit one
            // MAGDA clip per track per record pass.
            DBG("  dropping duplicate TE midi clip on track " << trackId);
            midiClip->removeFromParent();
        }

        if (pendingFinalizeMidi_.count(trackId) == 0) {
            pendingFinalizeMidi_.insert(trackId);
            juce::WeakReference<TracktionEngineWrapper> weakSelf(this);
            juce::MessageManager::callAsync([weakSelf, trackId]() {
                if (auto* self = weakSelf.get())
                    self->finalizeMidiRecording(trackId);
            });
        }
    }

    // For audio recordings, clear the preview + reset synths now (synchronous
    // path owns its clip creation). MIDI recordings are finalized via the
    // async callback — preview cleanup happens there. 0-clip finishes are TE
    // tearing an input instance down for a graph reallocation; another
    // aboutToStart will fire immediately and the preview must still be there
    // to animate.
    if (!recordedClips.isEmpty() && !pendingMidiRecordings_.count(trackId))
        recordingPreviews_.erase(trackId);

    if (trackId != INVALID_TRACK_ID && audioBridge_ && !pendingMidiRecordings_.count(trackId)) {
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

    // Sample metering data for audio-recording tracks
    if (audioBridge_) {
        auto& meteringBuffer = audioBridge_->getRecordingMeteringBuffer();
        for (auto& [trackId, preview] : recordingPreviews_) {
            if (!preview.isAudioRecording)
                continue;

            MeterData data;
            if (meteringBuffer.drainToLatest(trackId, data)) {
                preview.audioPeaks.push_back({data.peakL, data.peakR});
            }
        }
    }
}

void TracktionEngineWrapper::finalizeMidiRecording(TrackId trackId) {
    pendingFinalizeMidi_.erase(trackId);

    auto it = pendingMidiRecordings_.find(trackId);
    if (it == pendingMidiRecordings_.end())
        return;

    tracktion::MidiClip::Ptr midiClip = it->second;
    pendingMidiRecordings_.erase(it);

    if (!midiClip) {
        recordingPreviews_.erase(trackId);
        return;
    }

    double startSeconds = midiClip->getPosition().getStart().inSeconds();
    double lengthSeconds = midiClip->getPosition().getLength().inSeconds();

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

    DBG("finalizeMidiRecording: track=" << trackId << " notes=" << (int)recordedNotes.size()
                                        << " cc=" << (int)recordedCC.size()
                                        << " pb=" << (int)recordedPB.size());

    midiClip->removeFromParent();

    auto& clipManager = ClipManager::getInstance();
    ClipId clipId =
        clipManager.createMidiClip(trackId, startSeconds, lengthSeconds, ClipView::Arrangement);
    activeRecordingClips_[trackId] = clipId;

    if (auto* clipInfo = clipManager.getClip(clipId)) {
        clipInfo->midiNotes = std::move(recordedNotes);
        clipInfo->midiCCData = std::move(recordedCC);
        clipInfo->midiPitchBendData = std::move(recordedPB);
    }

    if (audioBridge_)
        audioBridge_->syncClipToEngine(clipId);

    recordingPreviews_.erase(trackId);

    if (trackId != INVALID_TRACK_ID && audioBridge_)
        audioBridge_->resetSynthsOnTrack(trackId);
}

}  // namespace magda
