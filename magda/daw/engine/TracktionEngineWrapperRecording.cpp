#include <cmath>

#include "../audio/AudioBridge.hpp"
#include "../core/ClipManager.hpp"
#include "../core/SelectionManager.hpp"
#include "../core/TempoUtils.hpp"
#include "../core/TrackManager.hpp"
#include "../project/ProjectManager.hpp"
#include "TracktionEngineWrapper.hpp"

namespace magda {

namespace {

double getBeatsPerBar(TracktionEngineWrapper& engine) {
    int numerator = 4;
    int denominator = 4;
    engine.getTimeSignature(numerator, denominator);
    if (numerator <= 0)
        numerator = 4;
    if (denominator <= 0)
        denominator = 4;
    return static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
}

double snapLengthToBars(TracktionEngineWrapper& engine, double lengthBeats) {
    const double beatsPerBar = getBeatsPerBar(engine);
    if (beatsPerBar <= 0.0)
        return juce::jmax(0.25, lengthBeats);

    return juce::jmax(beatsPerBar, std::ceil(lengthBeats / beatsPerBar) * beatsPerBar);
}

// Convert a transport position (seconds) to timeline beats via the edit's tempo
// sequence, so recording-preview placement stays correct across tempo changes
// (a constant tempo/60 factor would drift). Returns 0 if no edit is loaded.
double transportSecondsToBeats(tracktion::Edit* edit, double seconds) {
    if (!edit)
        return 0.0;
    return edit->tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(seconds)).inBeats();
}

}  // namespace

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
                preview.target = RecordingTargetKind::Arrangement;
                preview.startBeat =
                    transportSecondsToBeats(currentEdit_.get(), recordingStartTimes_[trackId]);
                preview.currentLengthBeats = 0.0;

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
            TrackId audioTrackId = trackId;
            if (auto* slot = audioClip->getClipSlot())
                audioTrackId = audioBridge_->getTrackIdForTeTrack(slot->track.itemID);

            if (audioTrackId == INVALID_TRACK_ID) {
                if (auto* teTrack = dynamic_cast<tracktion::AudioTrack*>(audioClip->getTrack()))
                    audioTrackId = audioBridge_->getTrackIdForTeTrack(teTrack->itemID);
            }

            const auto* audioTrackInfo = TrackManager::getInstance().getTrack(audioTrackId);
            const bool audioTrackHasInput =
                audioTrackInfo && !audioTrackInfo->audioInputDevice.isEmpty();

            if (!audioTrackHasInput) {
                DBG("  skipping audio clip — track has no audio input configured");
                audioClip->removeFromParent();
                continue;
            }

            if (audioTrackId == INVALID_TRACK_ID)
                continue;

            trackId = audioTrackId;

            if (finalizeSessionSlotAudioRecording(trackId, *audioClip))
                continue;

            juce::String audioFilePath = audioClip->getOriginalFile().getFullPathName();
            double startSeconds = audioClip->getPosition().getStart().inSeconds();
            double lengthSeconds = audioClip->getPosition().getLength().inSeconds();

            // NOTE: loop recording is loop-aligned. Tracktion anchors the clip
            // at the loop region and splits the capture into one take per loop
            // pass, so if recording started before the loop, the pre-loop
            // lead-in is NOT preserved as part of a take. By design: our take
            // model (AudioTake) has no per-take time offset and comping assumes
            // all takes share the clip start (take 0 at t=0), so loop-only takes
            // could not be aligned inside a clip that began before the loop.
            // Recording before the loop with Loop on therefore yields a
            // loop-region clip, not a lead-in plus takes.

            // Loop recording: Tracktion split the continuous capture into one
            // take (audio file) per loop pass and registered them on the clip.
            // Harvest them before we drop TE's clip, so no pass is lost. Each
            // take is a direct file reference (we record outside a TE project),
            // so resolve via SourceFileReference. The active take is the last
            // full pass; only the final pass can be partial (mid-pass stop).
            std::vector<AudioTake> takes;
            int activeTakeIndex = 0;
            if (audioClip->hasAnyTakes()) {
                auto takesTree = audioClip->state.getChildWithName(tracktion::IDs::TAKES);
                for (int i = 0; i < takesTree.getNumChildren(); ++i) {
                    auto takeChild = takesTree.getChild(i);
                    tracktion::SourceFileReference sfr(audioClip->edit, takeChild,
                                                       tracktion::IDs::source);
                    juce::File takeFile = sfr.getFile();
                    if (takeFile == juce::File())
                        continue;
                    AudioTake take;
                    take.filePath = takeFile.getFullPathName();
                    take.durationSeconds =
                        tracktion::AudioFile(audioClip->edit.engine, takeFile).getLength();
                    takes.push_back(take);
                }

                if (!takes.empty()) {
                    double loopLen = 0.0;
                    for (const auto& t : takes)
                        loopLen = std::max(loopLen, t.durationSeconds);
                    activeTakeIndex = static_cast<int>(takes.size()) - 1;
                    if (takes.size() > 1 && takes[activeTakeIndex].durationSeconds < loopLen * 0.95)
                        activeTakeIndex = static_cast<int>(takes.size()) - 2;

                    // Front the active take as the clip's source + length.
                    audioFilePath = takes[activeTakeIndex].filePath;
                    if (takes[activeTakeIndex].durationSeconds > 0.0)
                        lengthSeconds = takes[activeTakeIndex].durationSeconds;
                }
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
                if (isValidBpm(projectBPM)) {
                    newClip->audio().interpretation.bpm = projectBPM;
                    newClip->audio().interpretation.totalBeats = lengthSeconds * projectBPM / 60.0;
                }
                if (!takes.empty()) {
                    newClip->audio().takes = std::move(takes);
                    newClip->audio().currentTakeIndex = activeTakeIndex;
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

        TrackId midiTrackId = trackId;
        if (auto* slot = midiClip->getClipSlot())
            midiTrackId = audioBridge_->getTrackIdForTeTrack(slot->track.itemID);

        const auto* midiTrackInfo = TrackManager::getInstance().getTrack(midiTrackId);
        const bool midiTrackHasInput = midiTrackInfo && !midiTrackInfo->midiInputDevice.isEmpty();

        if (!midiTrackHasInput) {
            DBG("  skipping MIDI clip — track has no MIDI input configured");
            midiClip->removeFromParent();
            continue;
        }

        if (midiTrackId == INVALID_TRACK_ID) {
            if (auto* teTrack = dynamic_cast<tracktion::AudioTrack*>(midiClip->getTrack()))
                midiTrackId = audioBridge_->getTrackIdForTeTrack(teTrack->itemID);
        }

        if (midiTrackId == INVALID_TRACK_ID)
            continue;

        if (midiClip->getPosition().getLength().inSeconds() <= 0.0) {
            midiClip->removeFromParent();
            continue;
        }

        trackId = midiTrackId;

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
    tracktion::Edit* edit = currentEdit_.get();

    int eventsPopped = 0;
    RecordingNoteEvent evt;
    while (recordingNoteQueue_.pop(evt)) {
        eventsPopped++;
        TrackId trackId = evt.trackId;
        auto it = recordingPreviews_.find(trackId);
        if (it == recordingPreviews_.end())
            continue;

        auto& preview = it->second;
        // Note position relative to the pass start, in beats (tempo-correct).
        double eventBeat = transportSecondsToBeats(edit, evt.transportSeconds) - preview.startBeat;

        if (evt.isNoteOn) {
            MidiNote mn;
            mn.noteNumber = evt.noteNumber;
            mn.velocity = evt.velocity;
            mn.startBeat = eventBeat;
            mn.lengthBeats = -1.0;  // Sentinel: note still held
            preview.notes.push_back(mn);
        } else {
            // Note-off: find matching open note (same noteNumber, lengthBeats < 0)
            for (int i = static_cast<int>(preview.notes.size()) - 1; i >= 0; --i) {
                auto& n = preview.notes[static_cast<size_t>(i)];
                if (n.noteNumber == evt.noteNumber && n.lengthBeats < 0.0) {
                    n.lengthBeats = eventBeat - n.startBeat;
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

    // Grow each preview's length to match the playhead and sample its waveform.
    double currentBeat = transportSecondsToBeats(edit, getCurrentPosition());
    for (auto& [trackId, preview] : recordingPreviews_) {
        double newLength = currentBeat - preview.startBeat;
        const bool extending = newLength > preview.currentLengthBeats;
        if (extending)
            preview.currentLengthBeats = newLength;

        if (preview.isAudioRecording && audioBridge_) {
            MeterData data;
            if (audioBridge_->getRecordingMeteringBuffer().drainToLatest(trackId, data)) {
                const AudioPeakSample peak{data.peakL, data.peakR};
                if (extending || preview.audioPeaks.empty() || preview.currentLengthBeats <= 0.0) {
                    // First pass (incl. any pre-loop lead-in): append as the
                    // preview grows.
                    preview.audioPeaks.push_back(peak);
                } else {
                    // Loop recording wrapped the playhead back: the preview
                    // length is frozen at the first pass, so overwrite the peak
                    // at the current playhead position instead of appending.
                    // Each loop pass then redraws its own take live — the
                    // pre-loop lead-in stays put while the loop region updates
                    // every cycle — rather than the preview freezing or every
                    // pass piling into the same rectangle (which looked like the
                    // waveform scrolling backwards past the loop end).
                    const int n = static_cast<int>(preview.audioPeaks.size());
                    const double frac =
                        (currentBeat - preview.startBeat) / preview.currentLengthBeats;
                    const int idx = juce::jlimit(0, n - 1, static_cast<int>(frac * n));
                    preview.audioPeaks[static_cast<size_t>(idx)] = peak;
                }
            }
        }
    }
}

void TracktionEngineWrapper::armSessionSlotRecording(TrackId trackId, int sceneIndex) {
    if (trackId == INVALID_TRACK_ID || sceneIndex < 0)
        return;

    auto& clipManager = ClipManager::getInstance();
    if (clipManager.getClipInSlot(trackId, sceneIndex) != INVALID_CLIP_ID)
        return;

    auto it = sessionSlotRecordingTargets_.find(trackId);
    if (it != sessionSlotRecordingTargets_.end() && it->second.sceneIndex == sceneIndex &&
        !it->second.active) {
        sessionSlotRecordingTargets_.erase(it);
        recordingPreviews_.erase(trackId);
        return;
    }

    SessionSlotRecordingTarget target;
    target.sceneIndex = sceneIndex;
    sessionSlotRecordingTargets_[trackId] = target;
}

bool TracktionEngineWrapper::isSessionSlotRecordArmed(TrackId trackId, int sceneIndex) const {
    auto it = sessionSlotRecordingTargets_.find(trackId);
    return it != sessionSlotRecordingTargets_.end() && it->second.sceneIndex == sceneIndex;
}

bool TracktionEngineWrapper::isSessionSlotRecording(TrackId trackId, int sceneIndex) const {
    auto it = sessionSlotRecordingTargets_.find(trackId);
    return it != sessionSlotRecordingTargets_.end() && it->second.sceneIndex == sceneIndex &&
           it->second.active;
}

bool TracktionEngineWrapper::hasActiveSessionSlotRecordings() const {
    for (const auto& [trackId, target] : sessionSlotRecordingTargets_) {
        juce::ignoreUnused(trackId);
        if (target.active)
            return true;
    }
    return false;
}

void TracktionEngineWrapper::beginArmedSessionSlotRecordings() {
    if (sessionSlotRecordingTargets_.empty())
        return;

    bool startedAny = false;
    auto& clipManager = ClipManager::getInstance();

    for (auto it = sessionSlotRecordingTargets_.begin();
         it != sessionSlotRecordingTargets_.end();) {
        TrackId trackId = it->first;
        auto& target = it->second;

        if (target.active) {
            ++it;
            continue;
        }

        if (clipManager.getClipInSlot(trackId, target.sceneIndex) != INVALID_CLIP_ID) {
            it = sessionSlotRecordingTargets_.erase(it);
            continue;
        }

        const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (!trackInfo || !trackInfo->recordArmed) {
            ++it;
            continue;
        }

        const bool hasMidiInput = !trackInfo->midiInputDevice.isEmpty();
        const bool hasAudioInput = !trackInfo->audioInputDevice.isEmpty();

        bool armedTarget = false;
        if (audioBridge_) {
            if (hasMidiInput)
                armedTarget |= audioBridge_->setSessionSlotMidiRecordingTarget(
                    trackId, target.sceneIndex, true);
            if (hasAudioInput)
                armedTarget |= audioBridge_->setSessionSlotAudioRecordingTarget(
                    trackId, target.sceneIndex, true);
        }

        if (!armedTarget) {
            ++it;
            continue;
        }

        target.active = true;
        startedAny = true;

        // Create the transient active-recording-pass preview for this slot so the
        // session path shares the same beat-domain model as arrangement recording.
        createSessionSlotPreview(trackId, target.sceneIndex);

        ++it;
    }

    if (startedAny)
        recordingNoteQueue_.clear();
}

void TracktionEngineWrapper::createSessionSlotPreview(TrackId trackId, int sceneIndex) {
    // MIDI notes populate via drainRecordingNoteQueue (keyed by trackId) and audio
    // peaks via the recording metering buffer; both are already keyed by trackId,
    // so no extra wiring is needed beyond creating the preview entry.
    const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);

    RecordingPreview preview;
    preview.trackId = trackId;
    preview.target = RecordingTargetKind::SessionSlot;
    preview.sceneIndex = sceneIndex;
    preview.startBeat = transportSecondsToBeats(currentEdit_.get(), getCurrentPosition());
    preview.currentLengthBeats = 0.0;
    preview.isAudioRecording = trackInfo && !trackInfo->audioInputDevice.isEmpty();
    recordingPreviews_[trackId] = std::move(preview);
}

void TracktionEngineWrapper::finishSessionSlotRecordings() {
    if (!hasActiveSessionSlotRecordings())
        return;

    for (auto it = sessionSlotRecordingTargets_.begin();
         it != sessionSlotRecordingTargets_.end();) {
        TrackId trackId = it->first;
        const auto target = it->second;

        if (!target.active) {
            ++it;
            continue;
        }

        if (pendingFinalizeMidi_.count(trackId) > 0) {
            ++it;
            continue;
        }

        const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        const bool hasMidiInput = trackInfo && !trackInfo->midiInputDevice.isEmpty();
        const bool hasAudioInput = trackInfo && !trackInfo->audioInputDevice.isEmpty();

        if (hasMidiInput)
            createEmptySessionSlotRecordingClip(trackId, target.sceneIndex);

        if (audioBridge_) {
            if (hasMidiInput || !trackInfo)
                audioBridge_->setSessionSlotMidiRecordingTarget(trackId, target.sceneIndex, false);
            if (hasAudioInput || !trackInfo)
                audioBridge_->setSessionSlotAudioRecordingTarget(trackId, target.sceneIndex, false);
        }
        // Tear down the transient preview pass alongside the recording target so
        // session-slot teardown is self-contained (does not rely on the
        // transport-stop clear() that usually follows).
        recordingPreviews_.erase(trackId);
        it = sessionSlotRecordingTargets_.erase(it);
    }
}

ClipId TracktionEngineWrapper::createEmptySessionSlotRecordingClip(TrackId trackId,
                                                                   int sceneIndex) {
    auto& clipManager = ClipManager::getInstance();
    if (clipManager.getClipInSlot(trackId, sceneIndex) != INVALID_CLIP_ID)
        return INVALID_CLIP_ID;

    const double lengthBeats = getBeatsPerBar(*this);
    ClipId clipId = clipManager.createMidiClipBeats(trackId, 0.0, lengthBeats, ClipView::Session);
    if (clipId == INVALID_CLIP_ID)
        return INVALID_CLIP_ID;

    clipManager.setClipSceneIndex(clipId, sceneIndex);
    if (auto* clip = clipManager.getClip(clipId)) {
        const double tempo = getTempo() > 0.0 ? getTempo() : 120.0;
        clip->loopEnabled = true;
        clip->loopLengthBeats = lengthBeats;
        clip->loopLength = clip->getTimelineLength(tempo);
    }
    SelectionManager::getInstance().selectClip(clipId);
    clipManager.forceNotifyClipPropertyChanged(clipId);
    return clipId;
}

bool TracktionEngineWrapper::finalizeSessionSlotAudioRecording(
    TrackId trackId, tracktion::WaveAudioClip& audioClip) {
    auto targetIt = sessionSlotRecordingTargets_.find(trackId);
    if (targetIt == sessionSlotRecordingTargets_.end() || !targetIt->second.active)
        return false;

    auto* slot = audioClip.getClipSlot();
    if (!slot || slot->getIndex() != targetIt->second.sceneIndex)
        return false;

    const auto audioFile = audioClip.getOriginalFile();
    if (!audioFile.existsAsFile())
        return false;

    double lengthBeats = audioClip.getLengthBeats().inBeats();
    lengthBeats = snapLengthToBars(*this, lengthBeats);

    const double projectBpm = getTempo() > 0.0 ? getTempo() : 120.0;
    if (auto* edit = currentEdit_.get()) {
        auto end = edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(lengthBeats));
        audioClip.setLength(tracktion::toDuration(end), false);

        auto waveInfo = audioClip.getWaveInfo();
        auto& loopInfo = audioClip.getLoopInfo();
        loopInfo.setBpm(projectBpm, waveInfo);
        loopInfo.setNumBeats(lengthBeats);
        audioClip.setAutoTempo(true);
        audioClip.setLoopRangeBeats({tracktion::BeatPosition::fromBeats(0.0),
                                     tracktion::BeatPosition::fromBeats(lengthBeats)});

        if (auto launchHandle = audioClip.getLaunchHandle())
            launchHandle->setLooping(tracktion::BeatDuration::fromBeats(lengthBeats));
    }

    auto& clipManager = ClipManager::getInstance();
    ClipId clipId = clipManager.getClipInSlot(trackId, targetIt->second.sceneIndex);
    if (clipId == INVALID_CLIP_ID) {
        clipId = clipManager.createAudioClipBeats(
            trackId, 0.0, lengthBeats, audioFile.getFullPathName(), ClipView::Session, projectBpm);
        if (clipId != INVALID_CLIP_ID)
            clipManager.setClipSceneIndex(clipId, targetIt->second.sceneIndex);
    }

    if (auto* clipInfo = clipManager.getClip(clipId)) {
        clipInfo->setPlacementBeats(0.0, lengthBeats);
        clipInfo->deriveTimesFromBeats(projectBpm);
        clipInfo->autoTempo = true;
        clipInfo->loopEnabled = true;
        clipInfo->loopStartBeats = 0.0;
        clipInfo->loopLengthBeats = lengthBeats;
        clipInfo->loopStart = 0.0;
        clipInfo->loopLength = clipInfo->getTimelineLength(projectBpm);
        clipInfo->audio().interpretation.bpm = projectBpm;
        clipInfo->audio().interpretation.totalBeats = lengthBeats;
        clipInfo->audio().interpretation.totalBeatsLocked = true;
        clipManager.forceNotifyClipPropertyChanged(clipId);
        SelectionManager::getInstance().selectClip(clipId);
    }

    activeRecordingClips_[trackId] = clipId;
    recordingPreviews_.erase(trackId);
    if (audioBridge_)
        audioBridge_->setSessionSlotAudioRecordingTarget(trackId, targetIt->second.sceneIndex,
                                                         false);
    sessionSlotRecordingTargets_.erase(targetIt);
    return true;
}

bool TracktionEngineWrapper::finalizeSessionSlotMidiRecording(TrackId trackId,
                                                              tracktion::MidiClip& midiClip) {
    auto targetIt = sessionSlotRecordingTargets_.find(trackId);
    if (targetIt == sessionSlotRecordingTargets_.end() || !targetIt->second.active)
        return false;

    auto* slot = midiClip.getClipSlot();
    if (!slot || slot->getIndex() != targetIt->second.sceneIndex)
        return false;

    std::vector<MidiNote> recordedNotes;
    std::vector<MidiCCData> recordedCC;
    std::vector<MidiPitchBendData> recordedPB;

    // Defensive cap: an un-terminated note in TE's MidiList would otherwise
    // come through with a huge length, and the clip-length extension below
    // (jmax over note.startBeat + note.lengthBeats) would then stretch the
    // session slot to match — producing a perma-on note. Clamp each note to
    // TE's reported recording window so the worst case is a slightly-too-long
    // note that releases on the loop boundary instead of running forever.
    const double recordingEndBeats = midiClip.getLengthInBeats().inBeats();

    auto& midiList = midiClip.getSequence();
    for (auto* note : midiList.getNotes()) {
        if (!note)
            continue;
        MidiNote mn;
        mn.noteNumber = note->getNoteNumber();
        mn.velocity = note->getVelocity();
        mn.startBeat = note->getStartBeat().inBeats();
        mn.lengthBeats = note->getLengthBeats().inBeats();

        if (recordingEndBeats > 0.0) {
            if (mn.startBeat >= recordingEndBeats)
                continue;  // Note starts past the recording window — skip.
            const double maxLength = recordingEndBeats - mn.startBeat;
            if (mn.lengthBeats > maxLength || mn.lengthBeats <= 0.0)
                mn.lengthBeats = maxLength;
        }

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

    double lengthBeats = midiClip.getLengthInBeats().inBeats();
    for (const auto& note : recordedNotes)
        lengthBeats = juce::jmax(lengthBeats, note.startBeat + note.lengthBeats);
    for (const auto& cc : recordedCC)
        lengthBeats = juce::jmax(lengthBeats, cc.beatPosition);
    for (const auto& pb : recordedPB)
        lengthBeats = juce::jmax(lengthBeats, pb.beatPosition);
    lengthBeats = snapLengthToBars(*this, lengthBeats);

    if (auto* edit = currentEdit_.get()) {
        auto end = edit->tempoSequence.toTime(tracktion::BeatPosition::fromBeats(lengthBeats));
        midiClip.setLength(tracktion::toDuration(end), false);
        midiClip.setLoopRangeBeats({tracktion::BeatPosition::fromBeats(0.0),
                                    tracktion::BeatPosition::fromBeats(lengthBeats)});
        if (auto launchHandle = midiClip.getLaunchHandle())
            launchHandle->setLooping(tracktion::BeatDuration::fromBeats(lengthBeats));
    }

    auto& clipManager = ClipManager::getInstance();
    ClipId clipId = clipManager.getClipInSlot(trackId, targetIt->second.sceneIndex);
    if (clipId == INVALID_CLIP_ID) {
        clipId = clipManager.createMidiClipBeats(trackId, 0.0, lengthBeats, ClipView::Session);
        if (clipId != INVALID_CLIP_ID)
            clipManager.setClipSceneIndex(clipId, targetIt->second.sceneIndex);
    }

    if (auto* clipInfo = clipManager.getClip(clipId)) {
        const double tempo = getTempo() > 0.0 ? getTempo() : 120.0;
        clipInfo->setPlacementBeats(0.0, lengthBeats);
        clipInfo->deriveTimesFromBeats(tempo);
        clipInfo->loopEnabled = true;
        clipInfo->loopLengthBeats = lengthBeats;
        clipInfo->loopLength = clipInfo->getTimelineLength(tempo);
        clipInfo->midiNotes = std::move(recordedNotes);
        clipInfo->midiCCData = std::move(recordedCC);
        clipInfo->midiPitchBendData = std::move(recordedPB);
        clipManager.forceNotifyClipPropertyChanged(clipId);
        SelectionManager::getInstance().selectClip(clipId);
    }

    recordingPreviews_.erase(trackId);
    if (audioBridge_) {
        audioBridge_->resetSynthsOnTrack(trackId);
        audioBridge_->setSessionSlotMidiRecordingTarget(trackId, targetIt->second.sceneIndex,
                                                        false);
    }
    sessionSlotRecordingTargets_.erase(targetIt);
    return true;
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

    if (finalizeSessionSlotMidiRecording(trackId, *midiClip))
        return;

    double startSeconds = midiClip->getPosition().getStart().inSeconds();
    double lengthSeconds = midiClip->getPosition().getLength().inSeconds();

    // Extract a TE MidiList (one take's sequence) into a MAGDA MidiTake.
    auto extractTake = [](tracktion::MidiList& midiList) {
        MidiTake take;
        for (auto* note : midiList.getNotes()) {
            if (!note)
                continue;
            MidiNote mn;
            mn.noteNumber = note->getNoteNumber();
            mn.velocity = note->getVelocity();
            mn.startBeat = note->getStartBeat().inBeats();
            mn.lengthBeats = note->getLengthBeats().inBeats();
            take.notes.push_back(mn);
        }
        for (auto* ce : midiList.getControllerEvents()) {
            if (!ce)
                continue;
            int eventType = ce->getType();
            if (eventType == tracktion::MidiControllerEvent::pitchWheelType) {
                MidiPitchBendData pb;
                pb.value = ce->getControllerValue();
                pb.beatPosition = ce->getBeatPosition().inBeats();
                take.pitchBend.push_back(pb);
            } else if (eventType < 128) {
                MidiCCData cc;
                cc.controller = eventType;
                cc.value = ce->getControllerValue();
                cc.beatPosition = ce->getBeatPosition().inBeats();
                take.cc.push_back(cc);
            }
        }
        return take;
    };

    // Loop recording: each pass is a TE take (channelSequence entry). Harvest
    // them all so no pass is lost; a single (non-loop) recording has none, in
    // which case the active sequence is the only content.
    std::vector<MidiTake> takes;
    if (midiClip->hasAnyTakes()) {
        const int numTakes = midiClip->getNumTakes(/*includeComps=*/false);
        for (int i = 0; i < numTakes; ++i) {
            if (auto* takeList = midiClip->getTakeSequence(i))
                takes.push_back(extractTake(*takeList));
        }
    }

    // Active take = the last full pass. Only the final pass can be partial (a
    // mid-loop stop); if it has fewer notes than the prior pass, fall back to
    // that one. Mirrors the audio take heuristic.
    int activeTakeIndex = 0;
    if (takes.size() > 1) {
        activeTakeIndex = static_cast<int>(takes.size()) - 1;
        if (takes[activeTakeIndex].notes.size() <
            takes[static_cast<size_t>(activeTakeIndex) - 1].notes.size())
            activeTakeIndex -= 1;
    }

    // The content the clip plays: the active take, or the single recorded
    // sequence when there are no takes.
    MidiTake active = takes.empty() ? extractTake(midiClip->getSequence())
                                    : takes[static_cast<size_t>(activeTakeIndex)];

    DBG("finalizeMidiRecording: track=" << trackId << " takes=" << (int)takes.size()
                                        << " active=" << activeTakeIndex
                                        << " notes=" << (int)active.notes.size());

    midiClip->removeFromParent();

    auto& clipManager = ClipManager::getInstance();
    ClipId clipId =
        clipManager.createMidiClip(trackId, startSeconds, lengthSeconds, ClipView::Arrangement);
    activeRecordingClips_[trackId] = clipId;

    if (auto* clipInfo = clipManager.getClip(clipId)) {
        clipInfo->midiNotes = active.notes;
        clipInfo->midiCCData = active.cc;
        clipInfo->midiPitchBendData = active.pitchBend;
        if (takes.size() > 1) {
            clipInfo->midi().takes = std::move(takes);
            clipInfo->midi().currentTakeIndex = activeTakeIndex;
        }
    }

    if (audioBridge_)
        audioBridge_->syncClipToEngine(clipId);

    recordingPreviews_.erase(trackId);

    if (trackId != INVALID_TRACK_ID && audioBridge_)
        audioBridge_->resetSynthsOnTrack(trackId);
}

}  // namespace magda
