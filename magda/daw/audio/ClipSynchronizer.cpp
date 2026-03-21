#include "ClipSynchronizer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_set>

#include "../core/ClipManager.hpp"
#include "../core/ClipOperations.hpp"
#include "../core/TrackManager.hpp"
#include "TrackController.hpp"
#include "WarpMarkerManager.hpp"

namespace magda {

void ClipSynchronizer::reallocateAndNotify() {
    if (auto* ctx = edit_.getCurrentPlaybackContext()) {
        ctx->reallocate();
        if (onGraphReallocated)
            onGraphReallocated();
    }
}

// Map our LaunchQuantize enum to Tracktion Engine's LaunchQType
static te::LaunchQType toTELaunchQType(LaunchQuantize q) {
    switch (q) {
        case LaunchQuantize::None:
            return te::LaunchQType::none;
        case LaunchQuantize::EightBars:
            return te::LaunchQType::eightBars;
        case LaunchQuantize::FourBars:
            return te::LaunchQType::fourBars;
        case LaunchQuantize::TwoBars:
            return te::LaunchQType::twoBars;
        case LaunchQuantize::OneBar:
            return te::LaunchQType::bar;
        case LaunchQuantize::HalfBar:
            return te::LaunchQType::half;
        case LaunchQuantize::QuarterBar:
            return te::LaunchQType::quarter;
        case LaunchQuantize::EighthBar:
            return te::LaunchQType::eighth;
        case LaunchQuantize::SixteenthBar:
            return te::LaunchQType::sixteenth;
    }
    return te::LaunchQType::none;
}

ClipSynchronizer::ClipSynchronizer(te::Edit& edit, TrackController& trackController,
                                   WarpMarkerManager& warpMarkerManager)
    : edit_(edit), trackController_(trackController), warpMarkerManager_(warpMarkerManager) {
    ClipManager::getInstance().addListener(this);
    TrackManager::getInstance().addListener(this);
}

ClipSynchronizer::~ClipSynchronizer() {
    ClipManager::getInstance().removeListener(this);
    TrackManager::getInstance().removeListener(this);
}

// =============================================================================
// TrackManagerListener Interface
// =============================================================================

void ClipSynchronizer::syncPlaybackModeToEngine(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    auto* audioTrack = trackController_.getAudioTrack(trackId);
    if (trackInfo && audioTrack) {
        bool newVal = (trackInfo->playbackMode == TrackPlaybackMode::Session);
        audioTrack->playSlotClips = newVal;
    }
}

void ClipSynchronizer::trackPropertyChanged(int trackId) {
    syncPlaybackModeToEngine(trackId);
}

// =============================================================================
// ClipManagerListener Interface
// =============================================================================

void ClipSynchronizer::clipsChanged() {
    auto& clipManager = ClipManager::getInstance();

    // Only sync arrangement clips - session clips are managed by SessionClipScheduler
    const auto& arrangementClips = clipManager.getArrangementClips();

    // Build set of current arrangement clip IDs for fast lookup
    std::unordered_set<ClipId> currentClipIds;
    for (const auto& clip : arrangementClips) {
        currentClipIds.insert(clip.id);
    }

    // Find arrangement clips that are in the engine but no longer in ClipManager (deleted)
    std::vector<ClipId> clipsToRemove;
    {
        juce::ScopedLock lock(clipLock_);
        for (const auto& [clipId, engineId] : clipIdToEngineId_) {
            if (currentClipIds.find(clipId) == currentClipIds.end()) {
                clipsToRemove.push_back(clipId);
            }
        }
    }

    // Remove deleted clips from engine
    for (ClipId clipId : clipsToRemove) {
        removeClipFromEngine(clipId);
    }

    // Sync remaining arrangement clips to engine (add new ones, update existing)
    for (const auto& clip : arrangementClips) {
        syncClipToEngine(clip.id);
    }

    // Sync session clips to ClipSlots
    const auto& sessionClips = clipManager.getSessionClips();
    bool sessionClipsSynced = false;
    for (const auto& clip : sessionClips) {
        if (syncSessionClipToSlot(clip.id)) {
            sessionClipsSynced = true;
        }
    }

    // Force graph rebuild if new session clips were moved into slots,
    // so SlotControlNode instances are created in the audio graph
    if (sessionClipsSynced) {
        // Ensure playback mode is Arrangement on tracks with no actively playing slots,
        // so arrangement clips remain audible after graph rebuild
        for (const auto& trackInfo : TrackManager::getInstance().getTracks()) {
            if (trackInfo.playbackMode != TrackPlaybackMode::Session)
                continue;
            auto* track = trackController_.getAudioTrack(trackInfo.id);
            if (!track)
                continue;
            bool anyPlaying = false;
            for (auto* slot : track->getClipSlotList().getClipSlots()) {
                if (auto* c = slot->getClip()) {
                    if (auto lh = c->getLaunchHandle()) {
                        if (lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing) {
                            anyPlaying = true;
                            break;
                        }
                    }
                }
            }
            if (!anyPlaying)
                TrackManager::getInstance().setTrackPlaybackMode(trackInfo.id,
                                                                 TrackPlaybackMode::Arrangement);
        }

        reallocateAndNotify();
    }
}

void ClipSynchronizer::clipPropertyChanged(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        DBG("ClipSynchronizer::clipPropertyChanged: clip " << clipId
                                                           << " not found in ClipManager");
        return;
    }
    if (clip->view == ClipView::Session) {
        // Session clip property changed (e.g. sceneIndex set after creation).
        // Try to sync it to a slot if not already synced.
        if (clip->sceneIndex >= 0) {
            bool synced = syncSessionClipToSlot(clipId);

            if (synced) {
                // New clip synced — rebuild graph so SlotControlNode is created
                if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                    ctx->reallocate();
                    if (onGraphReallocated)
                        onGraphReallocated();
                }
            } else {
                // Clip already synced — propagate property changes to TE clip.
                // Only update properties that have actually changed to avoid
                // disrupting a playing LaunchHandle.
                auto* teClip = getSessionTeClip(clipId);
                if (teClip) {
                    // Update clip length only if changed
                    auto currentLength = teClip->getPosition().getLength();
                    if (std::abs(currentLength.inSeconds() - clip->length) > 0.0001) {
                        teClip->setLength(te::TimeDuration::fromSeconds(clip->length), false);
                    }

                    // Update launch quantization (lightweight CachedValue, always safe)
                    auto* lq = teClip->getLaunchQuantisation();
                    if (lq) {
                        lq->type = toTELaunchQType(clip->launchQuantize);
                    }

                    // AutoTempo handling for audio clips
                    bool isAutoTempoAudio = clip->type == ClipType::Audio && clip->autoTempo;

                    if (isAutoTempoAudio) {
                        auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip);
                        if (audioClip)
                            configureSessionAutoTempo(audioClip, clip);
                    } else {
                        // Note: do NOT call setAutoTempo(false) here.
                        // TE's ClipOwner auto-enables autoTempo on session slot clips
                        // and toggling it breaks the audio pipeline.

                        // Time-based loop state (existing behavior)
                        if (clip->loopEnabled) {
                            if (clip->getSourceLength() > 0.0) {
                                teClip->setLoopRange(te::TimeRange(
                                    te::TimePosition::fromSeconds(clip->getTeLoopStart()),
                                    te::TimePosition::fromSeconds(clip->getTeLoopEnd())));
                            }
                        } else if (teClip->isLooping()) {
                            teClip->disableLooping();
                        }

                        // Neutralize embedded tempo metadata: set source BPM =
                        // project BPM so the auto-enabled autoTempo doesn't cause
                        // unwanted speed changes.
                        if (clip->type == ClipType::Audio) {
                            if (auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip)) {
                                double projectBpm =
                                    edit_.tempoSequence.getBpmAt(te::TimePosition());
                                auto& li = audioClip->getLoopInfo();
                                auto waveInfo = audioClip->getWaveInfo();
                                li.setBpm(projectBpm, waveInfo);
                            }
                        }
                    }

                    // Update looping on the launch handle
                    auto launchHandle = teClip->getLaunchHandle();
                    if (launchHandle) {
                        if (clip->loopEnabled) {
                            if (isAutoTempoAudio) {
                                // AutoTempo: loop beats come from beat fields
                                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                                auto [loopStartBeats, loopLengthBeats] =
                                    ClipOperations::getAutoTempoBeatRange(*clip, bpm);
                                if (loopLengthBeats > 0.0)
                                    launchHandle->setLooping(
                                        te::BeatDuration::fromBeats(loopLengthBeats));
                            } else {
                                double loopLengthSeconds =
                                    clip->getSourceLength() / clip->speedRatio;
                                double bps =
                                    edit_.tempoSequence.getBpmAt(te::TimePosition()) / 60.0;
                                double loopLengthBeats = loopLengthSeconds * bps;
                                launchHandle->setLooping(
                                    te::BeatDuration::fromBeats(loopLengthBeats));
                            }
                        } else {
                            launchHandle->setLooping(std::nullopt);
                        }
                    }

                    // Sync session-applicable audio clip properties
                    if (clip->type == ClipType::Audio) {
                        auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip);
                        if (audioClip) {
                            // Pitch
                            bool isAnalog = clip->isAnalogPitchActive();
                            if (clip->autoPitch != audioClip->getAutoPitch())
                                audioClip->setAutoPitch(isAnalog ? false : clip->autoPitch);
                            if (isAnalog) {
                                if (std::abs(audioClip->getPitchChange()) > 0.001f)
                                    audioClip->setPitchChange(0.0f);
                            } else {
                                if (std::abs(audioClip->getPitchChange() - clip->pitchChange) >
                                    0.001f)
                                    audioClip->setPitchChange(clip->pitchChange);
                            }
                            if (audioClip->getTransposeSemiTones(false) != clip->transpose)
                                audioClip->setTranspose(clip->transpose);
                            // Playback
                            if (clip->isReversed != audioClip->getIsReversed())
                                audioClip->setIsReversed(clip->isReversed);
                            // Per-Clip Mix
                            {
                                float combinedGain = clip->volumeDB + clip->gainDB;
                                if (std::abs(audioClip->getGainDB() - combinedGain) > 0.001f)
                                    audioClip->setGainDB(combinedGain);
                            }
                            if (std::abs(audioClip->getPan() - clip->pan) > 0.001f)
                                audioClip->setPan(clip->pan);
                        }
                    }

                    // Re-sync MIDI notes from ClipManager to the TE MidiClip
                    if (clip->type == ClipType::MIDI) {
                        if (auto* midiClip = dynamic_cast<te::MidiClip*>(teClip)) {
                            auto& sequence = midiClip->getSequence();
                            sequence.clear(nullptr);

                            // For MIDI, use clip length as boundary
                            double clipLengthBeats =
                                clip->length *
                                (edit_.tempoSequence.getBpmAt(te::TimePosition()) / 60.0);
                            for (const auto& note : clip->midiNotes) {
                                double start = note.startBeat;
                                double length = note.lengthBeats;

                                // Skip or truncate notes at the clip boundary
                                if (clip->loopEnabled) {
                                    if (start >= clipLengthBeats)
                                        continue;
                                    double noteEnd = start + length;
                                    if (noteEnd > clipLengthBeats)
                                        length = clipLengthBeats - start;
                                }

                                sequence.addNote(
                                    note.noteNumber, te::BeatPosition::fromBeats(start),
                                    te::BeatDuration::fromBeats(length), note.velocity, 0, nullptr);
                            }
                        }
                    }

                }  // if (teClip)
            }      // else (already synced)
        }          // if (sceneIndex >= 0)
        return;
    }

    syncClipToEngine(clipId);
}

void ClipSynchronizer::clipSelectionChanged(ClipId clipId) {
    // Selection changed - we don't need to do anything here
    // The UI will handle this
    juce::ignoreUnused(clipId);
}

// =============================================================================
// Arrangement Clip Operations
// =============================================================================

void ClipSynchronizer::syncClipToEngine(ClipId clipId) {
    auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        DBG("syncClipToEngine: Clip not found: " << clipId);
        return;
    }

    // Only sync arrangement clips - session clips are managed by SessionClipScheduler
    if (clip->view == ClipView::Session) {
        return;
    }

    // Route to appropriate sync method by type
    if (clip->type == ClipType::MIDI) {
        syncMidiClipToEngine(clipId, clip);
    } else if (clip->type == ClipType::Audio) {
        syncAudioClipToEngine(clipId, clip);
    } else {
        DBG("syncClipToEngine: Unknown clip type for clip " << clipId);
    }
}

void ClipSynchronizer::removeTeClipByEngineId(const std::string& engineId) {
    for (auto* track : tracktion::getAudioTracks(edit_)) {
        for (auto* clip : track->getClips()) {
            if (clip->itemID.toString().toStdString() == engineId) {
                clip->removeFromParent();
                return;
            }
        }
    }
}

void ClipSynchronizer::removeClipFromEngine(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);

    // Remove clip from engine
    auto it = clipIdToEngineId_.find(clipId);
    if (it == clipIdToEngineId_.end()) {
        DBG("removeClipFromEngine: Clip not in engine: " << clipId);
        return;
    }

    std::string engineId = it->second;

    // Find the clip in Tracktion Engine and remove it
    // We need to find which track contains this clip
    for (auto* track : tracktion::getAudioTracks(edit_)) {
        for (auto* clip : track->getClips()) {
            if (clip->itemID.toString().toStdString() == engineId) {
                // Found the clip - remove it
                clip->removeFromParent();

                // Remove from mappings
                clipIdToEngineId_.erase(it);
                engineIdToClipId_.erase(engineId);

                DBG("removeClipFromEngine: Removed clip " << clipId);
                return;
            }
        }
    }

    DBG("removeClipFromEngine: Clip not found in Tracktion Engine: " << engineId);
}

te::Clip* ClipSynchronizer::getArrangementTeClip(ClipId clipId) const {
    juce::ScopedLock lock(clipLock_);

    auto it = clipIdToEngineId_.find(clipId);
    if (it == clipIdToEngineId_.end())
        return nullptr;

    const auto& engineId = it->second;
    for (auto* track : te::getAudioTracks(edit_)) {
        for (auto* teClip : track->getClips()) {
            if (teClip->itemID.toString().toStdString() == engineId)
                return teClip;
        }
    }
    return nullptr;
}

// =============================================================================
// Session Clip Operations
// =============================================================================

bool ClipSynchronizer::syncSessionClipToSlot(ClipId clipId) {
    namespace te = tracktion;

    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip) {
        DBG("ClipSynchronizer::syncSessionClipToSlot: Clip " << clipId
                                                             << " not found in ClipManager");
        return false;
    }
    if (clip->view != ClipView::Session || clip->sceneIndex < 0)
        return false;

    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("ClipSynchronizer::syncSessionClipToSlot: Track " << clip->trackId
                                                              << " not found for clip " << clipId);
        return false;
    }

    // Ensure enough scenes (and slots on all tracks) exist
    edit_.getSceneList().ensureNumberOfScenes(clip->sceneIndex + 1);

    // Get the slot for this clip
    auto slots = audioTrack->getClipSlotList().getClipSlots();

    if (clip->sceneIndex >= static_cast<int>(slots.size())) {
        DBG("ClipSynchronizer::syncSessionClipToSlot: Slot index out of range for clip " << clipId);
        return false;
    }

    auto* slot = slots[clip->sceneIndex];
    if (!slot)
        return false;

    // If slot already has a clip, skip (already synced)
    if (slot->getClip() != nullptr)
        return false;

    // Create the TE clip directly in the slot (NOT on the track then moved).
    // TE's free functions insertWaveClip(ClipOwner&, ...) and insertMIDIClip(ClipOwner&, ...)
    // accept ClipSlot as a ClipOwner, creating the clip's ValueTree directly in the slot.
    if (clip->type == ClipType::Audio) {
        if (clip->audioFilePath.isEmpty())
            return false;

        juce::File audioFile(clip->audioFilePath);
        if (!audioFile.existsAsFile()) {
            DBG("ClipSynchronizer::syncSessionClipToSlot: Audio file not found: "
                << clip->audioFilePath);
            return false;
        }

        // Create clip directly in the slot
        double clipDuration = clip->length;
        auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                       te::TimePosition::fromSeconds(clipDuration));

        auto clipRef = te::insertWaveClip(*slot, audioFile.getFileNameWithoutExtension(), audioFile,
                                          te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

        if (!clipRef)
            return false;

        auto* audioClipPtr = clipRef.get();

        // Populate source file metadata from TE's loopInfo
        {
            auto& loopInfoRef = audioClipPtr->getLoopInfo();
            auto waveInfo = audioClipPtr->getWaveInfo();
            if (auto* mutableClip = cm.getClip(clipId))
                mutableClip->setSourceMetadata(loopInfoRef.getNumBeats(),
                                               loopInfoRef.getBpm(waveInfo));
        }

        if (clip->autoTempo) {
            configureSessionAutoTempo(audioClipPtr, clip);
        } else {
            // =============================================================
            // TIME-BASED MODE — existing behavior
            // =============================================================

            // Set timestretcher mode — keep disabled when mode is 0 and speedRatio is 1.0
            {
                bool isAnalog = clip->isAnalogPitchActive();
                auto stretchMode = static_cast<te::TimeStretcher::Mode>(clip->timeStretchMode);
                if (!isAnalog && stretchMode == te::TimeStretcher::disabled &&
                    (std::abs(clip->speedRatio - 1.0) > 0.001 || clip->warpEnabled))
                    stretchMode = te::TimeStretcher::defaultMode;
                if (isAnalog)
                    stretchMode = te::TimeStretcher::disabled;
                audioClipPtr->setTimeStretchMode(stretchMode);
            }

            // Set speed ratio (BEFORE offset, since TE offset
            // is in stretched time and must be set after speed ratio)
            if (std::abs(clip->speedRatio - 1.0) > 0.001) {
                if (audioClipPtr->getAutoTempo()) {
                    audioClipPtr->setAutoTempo(false);
                }
                audioClipPtr->setSpeedRatio(clip->speedRatio);
            }

            // Set file offset (trim point) - relative to loop start, in stretched time
            {
                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                audioClipPtr->setOffset(
                    te::TimeDuration::fromSeconds(clip->getTeOffset(clip->loopEnabled, bpm)));
            }

            // Set looping properties
            if (clip->loopEnabled && clip->getSourceLength() > 0.0) {
                audioClipPtr->setLoopRange(
                    te::TimeRange(te::TimePosition::fromSeconds(clip->getTeLoopStart()),
                                  te::TimePosition::fromSeconds(clip->getTeLoopEnd())));
            }

            // TE's ClipOwner auto-enables autoTempo on all session slot clips.
            // Neutralize embedded tempo metadata by setting source BPM = project
            // BPM so the stretch ratio is 1.0 and no unwanted speed change occurs.
            {
                double projectBpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                auto& li = audioClipPtr->getLoopInfo();
                auto waveInfo = audioClipPtr->getWaveInfo();
                li.setBpm(projectBpm, waveInfo);
            }
        }

        // Set per-clip launch quantization
        audioClipPtr->setUsesGlobalLaunchQuatisation(false);
        if (auto* lq = audioClipPtr->getLaunchQuantisation()) {
            lq->type = toTELaunchQType(clip->launchQuantize);
        }

        // Sync session-applicable audio properties at creation
        {
            bool isAnalog = clip->isAnalogPitchActive();
            if (!isAnalog && clip->autoPitch)
                audioClipPtr->setAutoPitch(true);
            if (isAnalog) {
                // Analog pitch: don't send pitchChange to TE (resampling handles it)
            } else if (std::abs(clip->pitchChange) > 0.001f) {
                audioClipPtr->setPitchChange(clip->pitchChange);
            }
        }
        if (clip->transpose != 0)
            audioClipPtr->setTranspose(clip->transpose);
        if (clip->isReversed)
            audioClipPtr->setIsReversed(true);
        {
            float combinedGain = clip->volumeDB + clip->gainDB;
            if (std::abs(combinedGain) > 0.001f)
                audioClipPtr->setGainDB(combinedGain);
        }
        if (std::abs(clip->pan) > 0.001f)
            audioClipPtr->setPan(clip->pan);

        // Set a small fade-in/out to prevent clicks on launch/stop transitions.
        // Session clips don't have user-configurable fades, so apply a minimal
        // ~2ms fade that's inaudible but prevents discontinuities.
        {
            double fadeInVal = (clip->fadeIn <= 0.0) ? 0.002 : clip->fadeIn;
            double fadeOutVal = (clip->fadeOut <= 0.0) ? 0.002 : clip->fadeOut;
            audioClipPtr->setFadeIn(te::TimeDuration::fromSeconds(fadeInVal));
            audioClipPtr->setFadeOut(te::TimeDuration::fromSeconds(fadeOutVal));
        }

        // Set LaunchHandle looping state at creation time so it's ready before first launch
        if (auto lh = audioClipPtr->getLaunchHandle()) {
            if (clip->loopEnabled) {
                if (clip->autoTempo) {
                    double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                    auto [loopStartBeats, loopLengthBeats] =
                        ClipOperations::getAutoTempoBeatRange(*clip, bpm);
                    if (loopLengthBeats > 0.0)
                        lh->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
                } else if (clip->getSourceLength() > 0.0) {
                    double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                    double loopDurationBeats =
                        (clip->getSourceLength() / clip->speedRatio) * (bpm / 60.0);
                    lh->setLooping(te::BeatDuration::fromBeats(loopDurationBeats));
                }
            }
        }

        // Force WarpTimeManager creation now so its constructor's warp marker
        // insertions (which trigger TreeWatcher → restartPlayback) happen during
        // initial sync rather than lazily during playback (which causes a click).
        audioClipPtr->getWarpTimeManager();

        return true;

    } else if (clip->type == ClipType::MIDI) {
        // Create MIDI clip directly in the slot
        double clipDuration = clip->length;
        auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(0.0),
                                       te::TimePosition::fromSeconds(clipDuration));

        auto clipRef = te::insertMIDIClip(*slot, timeRange);
        if (!clipRef)
            return false;

        auto* midiClipPtr = clipRef.get();

        // Force offset to 0 — note shifting is handled manually below
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

        // Add MIDI notes (skip/truncate at loop boundary to prevent stuck notes)
        // Apply midiOffset: exclude notes before offset, shift remaining notes
        auto& sequence = midiClipPtr->getSequence();
        double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
        double srcLength = clip->getSourceLength();
        double loopStartBeat = clip->loopStart * (bpm / 60.0);
        double loopLengthBeats = srcLength * (bpm / 60.0);
        double loopEndBeat = loopStartBeat + loopLengthBeats;
        double effectiveOffset = clip->midiOffset;

        for (const auto& note : clip->midiNotes) {
            double start = note.startBeat;
            double length = note.lengthBeats;

            // Apply loop boundary first (on original positions)
            if (clip->loopEnabled && loopLengthBeats > 0.0) {
                if (start >= loopEndBeat)
                    continue;
                double noteEnd = start + length;
                if (noteEnd > loopEndBeat)
                    length = loopEndBeat - start;
            }

            // Skip notes entirely before the offset
            if (start + length <= effectiveOffset)
                continue;

            // Shift note start by offset
            double shiftedStart = start - effectiveOffset;
            if (shiftedStart < 0.0) {
                length += shiftedStart;  // Trim the beginning
                shiftedStart = 0.0;
            }

            if (length > 0.0)
                sequence.addNote(note.noteNumber, te::BeatPosition::fromBeats(shiftedStart),
                                 te::BeatDuration::fromBeats(length), note.velocity, 0, nullptr);
        }

        // Set looping if enabled
        if (clip->loopEnabled) {
            midiClipPtr->setLoopRangeBeats({te::BeatPosition::fromBeats(loopStartBeat),
                                            te::BeatPosition::fromBeats(loopEndBeat)});
        }

        // Set per-clip launch quantization
        midiClipPtr->setUsesGlobalLaunchQuatisation(false);
        if (auto* lq = midiClipPtr->getLaunchQuantisation()) {
            lq->type = toTELaunchQType(clip->launchQuantize);
        }

        // Set LaunchHandle looping state at creation time
        if (auto lh = midiClipPtr->getLaunchHandle()) {
            if (clip->loopEnabled && loopLengthBeats > 0.0)
                lh->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
        }

        return true;
    }

    return false;
}

void ClipSynchronizer::removeSessionClipFromSlot(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (teClip)
        teClip->removeFromParent();
}

void ClipSynchronizer::launchSessionClip(ClipId clipId, bool forceImmediate) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip) {
        return;
    }

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle) {
        return;
    }

    // Update LaunchHandle looping state before play.
    // NOTE: Do NOT call teClip->setLoopRange() / setLoopRangeBeats() here!
    // Those modify clip ValueTree properties (loopStartBeats, loopLengthBeats,
    // autoTempo) which TE's TreeWatcher detects and calls restartPlayback(),
    // triggering a graph rebuild mid-playback that causes an audible click.
    // The clip's loop range is already set during syncSessionClipToSlot() and
    // kept up-to-date by clipPropertyChanged().
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        if (clip->loopEnabled) {
            double srcLength = clip->getSourceLength();
            if (clip->type == ClipType::Audio && clip->autoTempo) {
                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                auto [loopStartBeats, loopLengthBeats] =
                    ClipOperations::getAutoTempoBeatRange(*clip, bpm);
                if (loopLengthBeats > 0.0) {
                    launchHandle->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
                }
            } else if (clip->type == ClipType::Audio && srcLength > 0.0) {
                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                double loopDurationBeats = (srcLength / clip->speedRatio) * (bpm / 60.0);
                launchHandle->setLooping(te::BeatDuration::fromBeats(loopDurationBeats));
            } else {
                // MIDI
                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                double loopLengthBeats = srcLength * (bpm / 60.0);
                launchHandle->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
            }
        } else {
            launchHandle->setLooping(std::nullopt);
        }
    }

    // Switch track to session mode before launching so the arrangement is
    // already muted when the slot starts — prevents a brief audio overlap.
    if (clip) {
        TrackManager::getInstance().setTrackPlaybackMode(clip->trackId, TrackPlaybackMode::Session);
    }

    auto qType =
        (clip && !forceImmediate) ? toTELaunchQType(clip->launchQuantize) : te::LaunchQType::none;

    // Calculate the target beat (nullopt = immediate).
    // Add a lookahead margin so the target is always safely in the future
    // by the time the audio thread processes it. The SyncPoint is a snapshot
    // from the audio thread; message-thread latency + 2-3 audio buffers can
    // elapse before play() reaches the audio thread.
    std::optional<te::MonotonicBeat> targetBeat;
    if (qType != te::LaunchQType::none) {
        auto* ctx = edit_.getCurrentPlaybackContext();
        auto syncPoint = ctx ? ctx->getSyncPoint() : std::nullopt;
        if (syncPoint) {
            // Lookahead: ~50ms worth of beats to cover message-thread + audio-buffer latency
            double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
            double lookaheadBeats = (bpm > 0.0) ? (0.05 * bpm / 60.0) : 0.1;
            auto adjustedBeat = syncPoint->beat + te::BeatDuration::fromBeats(lookaheadBeats);

            auto quantizedBeat = te::getNext(qType, edit_.tempoSequence, adjustedBeat);
            double offset = syncPoint->monotonicBeat.v.inBeats() - syncPoint->beat.inBeats();
            targetBeat =
                te::MonotonicBeat{te::BeatPosition::fromBeats(quantizedBeat.inBeats() + offset)};
            // Store the precise quantized launch time for SessionRecorder
            if (clip) {
                double quantizedTime = edit_.tempoSequence.beatsToTime(quantizedBeat).inSeconds();
                lastLaunchTimeByTrack_[clip->trackId] = quantizedTime;
            }
        }
    }

    // Stop other clips on the same track:
    // - Playing clips: stop at the SAME target beat (no gap)
    // - Queued clips: cancel immediately (stop with nullopt)
    if (clip) {
        auto& cm = ClipManager::getInstance();
        for (const auto& otherClip : cm.getSessionClips()) {
            if (otherClip.trackId == clip->trackId && otherClip.id != clipId) {
                auto* otherTeClip = getSessionTeClip(otherClip.id);
                if (!otherTeClip)
                    continue;
                auto otherLH = otherTeClip->getLaunchHandle();
                if (!otherLH)
                    continue;
                auto otherPlayState = otherLH->getPlayingStatus();
                auto otherQueuedState = otherLH->getQueuedStatus();
                if (otherPlayState == te::LaunchHandle::PlayState::playing) {
                    otherLH->stop(targetBeat ? *targetBeat : std::optional<te::MonotonicBeat>{});
                } else if (otherQueuedState &&
                           *otherQueuedState == te::LaunchHandle::QueueState::playQueued) {
                    otherLH->stop(std::nullopt);
                }
            }
        }
    }

    if (!targetBeat) {
        // For immediate launches, use transport position as fallback
        if (clip) {
            lastLaunchTimeByTrack_[clip->trackId] = edit_.getTransport().position.get().inSeconds();
        }
        launchHandle->play(std::nullopt);
    } else {
        launchHandle->play(*targetBeat);
    }
}

double ClipSynchronizer::getLastLaunchTimeForTrack(TrackId trackId) const {
    auto it = lastLaunchTimeByTrack_.find(trackId);
    return (it != lastLaunchTimeByTrack_.end()) ? it->second : 0.0;
}

void ClipSynchronizer::stopSessionClip(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip) {
        return;
    }

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle) {
        return;
    }

    launchHandle->stop(std::nullopt);

    // Reset synth plugins to prevent stuck MIDI notes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    if (clip->type == ClipType::MIDI) {
        auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
        if (audioTrack) {
            for (auto* plugin : audioTrack->pluginList) {
                if (plugin->isSynth()) {
                    plugin->reset();
                }
            }
        }
    }
    // Track playback mode is managed by SessionClipScheduler, not here.
}

te::Clip* ClipSynchronizer::getSessionTeClip(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session || clip->sceneIndex < 0) {
        return nullptr;
    }

    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        return nullptr;
    }

    auto slots = audioTrack->getClipSlotList().getClipSlots();

    if (clip->sceneIndex >= static_cast<int>(slots.size())) {
        return nullptr;
    }

    auto* slot = slots[clip->sceneIndex];
    return slot ? slot->getClip() : nullptr;
}

// =============================================================================
// Session AutoTempo Helper
// =============================================================================

void ClipSynchronizer::configureSessionAutoTempo(te::WaveAudioClip* audioClip,
                                                 const ClipInfo* clip) {
    // Sync sourceBPM to TE's loopInfo
    if (clip->sourceBPM > 0.0) {
        auto waveInfo = audioClip->getWaveInfo();
        auto& li = audioClip->getLoopInfo();
        li.setBpm(clip->sourceBPM, waveInfo);
    }

    // Ensure valid stretch mode (autoTempo requires time-stretching)
    if (audioClip->getTimeStretchMode() == te::TimeStretcher::disabled)
        audioClip->setTimeStretchMode(te::TimeStretcher::defaultMode);

    // Force speedRatio to 1.0 (TE requirement for autoTempo)
    if (std::abs(audioClip->getSpeedRatio() - 1.0) > 0.001)
        audioClip->setSpeedRatio(1.0);

    // Enable autoTempo
    if (!audioClip->getAutoTempo())
        audioClip->setAutoTempo(true);

    // Set offset — for autoTempo, convert source seconds to timeline seconds
    double bpmForOffset = edit_.tempoSequence.getBpmAt(te::TimePosition());
    audioClip->setOffset(
        te::TimeDuration::fromSeconds(clip->getTeOffset(clip->loopEnabled, bpmForOffset)));

    // Set beat-based loop range using the same helper as arrangement path
    if (clip->loopEnabled) {
        double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
        auto [loopStartBeats, loopLengthBeats] = ClipOperations::getAutoTempoBeatRange(*clip, bpm);
        if (loopLengthBeats > 0.0) {
            audioClip->setLoopRangeBeats(
                te::BeatRange(te::BeatPosition::fromBeats(loopStartBeats),
                              te::BeatDuration::fromBeats(loopLengthBeats)));
        }
    } else if (audioClip->isLooping()) {
        audioClip->disableLooping();
    }
}

// =============================================================================
// Warp Marker Operations (Delegated to WarpMarkerManager)
// =============================================================================

// Helper: build a clip-ID-to-engine-ID map that works for both arrangement and
// session clips.  For arrangement clips the entry already exists in
// clipIdToEngineId_.  For session clips we resolve the TE clip via the slot and
// add a temporary entry so WarpMarkerManager's findWaveAudioClip() can find it.
std::map<ClipId, std::string> ClipSynchronizer::buildWarpClipMap(ClipId clipId) {
    // Start with the existing arrangement map
    auto map = clipIdToEngineId_;

    // If the clip is already in the map, nothing to do
    if (map.count(clipId))
        return map;

    // Try resolving as a session clip
    auto* teClip = getSessionTeClip(clipId);
    if (teClip) {
        map[clipId] = teClip->itemID.toString().toStdString();
    }

    return map;
}

void ClipSynchronizer::setTransientSensitivity(ClipId clipId, float sensitivity) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    warpMarkerManager_.setTransientSensitivity(edit_, map, clipId, sensitivity);
}

bool ClipSynchronizer::getTransientTimes(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    return warpMarkerManager_.getTransientTimes(edit_, map, clipId);
}

void ClipSynchronizer::enableWarp(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    warpMarkerManager_.enableWarp(edit_, map, clipId);
}

void ClipSynchronizer::disableWarp(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    warpMarkerManager_.disableWarp(edit_, map, clipId);
}

std::vector<WarpMarkerInfo> ClipSynchronizer::getWarpMarkers(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    return warpMarkerManager_.getWarpMarkers(edit_, map, clipId);
}

int ClipSynchronizer::addWarpMarker(ClipId clipId, double sourceTime, double warpTime) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    return warpMarkerManager_.addWarpMarker(edit_, map, clipId, sourceTime, warpTime);
}

double ClipSynchronizer::moveWarpMarker(ClipId clipId, int index, double newWarpTime) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    return warpMarkerManager_.moveWarpMarker(edit_, map, clipId, index, newWarpTime);
}

void ClipSynchronizer::removeWarpMarker(ClipId clipId, int index) {
    juce::ScopedLock lock(clipLock_);
    auto map = buildWarpClipMap(clipId);
    warpMarkerManager_.removeWarpMarker(edit_, map, clipId, index);
}

// =============================================================================
// CC/PitchBend Interpolation Helper
// =============================================================================

/**
 * @brief Generate interpolated CC/PB controller events between curve points.
 *
 * For Step curves, only the original event is emitted. For Linear (with tension)
 * and Bezier curves, intermediate events are generated every 1/64 beat to produce
 * smooth MIDI controller output instead of staircase steps.
 *
 * @tparam EventType  MidiCCData or MidiPitchBendData
 * @param sequence    The Tracktion MIDI sequence to add events to
 * @param events      Sorted events to interpolate between
 * @param controllerType  CC number or pitchWheelType
 * @param effectiveOffset Beat offset to subtract from positions
 * @param visibleStart    Start of visible range in beats
 * @param visibleEnd      End of visible range in beats
 * @param contentLengthBeats  Maximum beat position
 */
template <typename EventType>
static void interpolateCCEvents(te::MidiList& sequence, const std::vector<EventType>& events,
                                int controllerType, double effectiveOffset, double visibleStart,
                                double visibleEnd, double contentLengthBeats) {
    if (events.empty())
        return;

    // Make a sorted copy
    auto sorted = events;
    std::sort(sorted.begin(), sorted.end(), [](const EventType& a, const EventType& b) {
        return a.beatPosition < b.beatPosition;
    });

    constexpr double kStepSize = 1.0 / 64.0;  // 1/64 beat between interpolated events

    // Tracktion Engine stores all controller values in 14-bit range (0-16383).
    // CC values (0-127) must be left-shifted by 7 bits; pitch bend is already 14-bit.
    const bool isPitchBend = (controllerType == te::MidiControllerEvent::pitchWheelType);
    const int maxValue = isPitchBend ? 16383 : 127;

    auto addEvent = [&](double beatPos, int value) {
        double adjusted = beatPos - effectiveOffset;
        if (adjusted >= 0.0 && adjusted < contentLengthBeats) {
            int teValue = isPitchBend ? value : (value << 7);
            sequence.addControllerEvent(te::BeatPosition::fromBeats(adjusted), controllerType,
                                        teValue, nullptr);
        }
    };

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& ev = sorted[i];

        // Skip events outside visible range
        if (ev.beatPosition < visibleStart || ev.beatPosition >= visibleEnd)
            continue;

        if (ev.curveType == MidiCurveType::Step || i == sorted.size() - 1) {
            // Step: just emit the single event value (held until next event)
            // Also emit last event as-is since there's no next point to interpolate toward
            addEvent(ev.beatPosition, ev.value);
            continue;
        }

        // We have a next event to interpolate toward
        const auto& next = sorted[i + 1];
        double beatStart = ev.beatPosition;
        double beatEnd = next.beatPosition;
        double span = beatEnd - beatStart;

        if (span <= 0.0) {
            addEvent(ev.beatPosition, ev.value);
            continue;
        }

        double v1 = static_cast<double>(ev.value);
        double v2 = static_cast<double>(next.value);

        if (ev.curveType == MidiCurveType::Linear) {
            // Generate interpolated events every kStepSize beats
            double tension = ev.tension;
            for (double beat = beatStart; beat < beatEnd; beat += kStepSize) {
                if (beat < visibleStart || beat >= visibleEnd)
                    continue;

                double t = (beat - beatStart) / span;

                // Apply tension (same formula as CurveSnapshot::evaluate / CurveEditorBase)
                double curvedT;
                if (std::abs(tension) < 0.001) {
                    curvedT = t;
                } else if (tension > 0) {
                    curvedT = std::pow(t, 1.0 + tension * 2.0);
                } else {
                    curvedT = 1.0 - std::pow(1.0 - t, 1.0 - tension * 2.0);
                }

                int val =
                    std::clamp(static_cast<int>(std::round(v1 + curvedT * (v2 - v1))), 0, maxValue);
                addEvent(beat, val);
            }
        } else if (ev.curveType == MidiCurveType::Bezier) {
            // Cubic bezier interpolation using in/out handles
            // Control points in normalized (beat, value) space:
            //   P0 = (beatStart, v1)
            //   P1 = (beatStart + outHandle.dx, v1 + outHandle.dy * valueRange)
            //   P2 = (beatEnd + inHandle.dx, v2 + inHandle.dy * valueRange)
            //   P3 = (beatEnd, v2)
            double p0x = beatStart, p0y = v1;
            double p1x = beatStart + ev.outHandle.dx;
            double p1y = v1 + ev.outHandle.dy * (v2 - v1);
            double p2x = beatEnd + next.inHandle.dx;
            double p2y = v2 + next.inHandle.dy * (v2 - v1);
            double p3x = beatEnd, p3y = v2;

            juce::ignoreUnused(p0x, p1x, p2x, p3x);

            for (double beat = beatStart; beat < beatEnd; beat += kStepSize) {
                if (beat < visibleStart || beat >= visibleEnd)
                    continue;

                double t = (beat - beatStart) / span;

                // Cubic bezier: B(t) = (1-t)^3*P0 + 3(1-t)^2*t*P1 + 3(1-t)*t^2*P2 + t^3*P3
                double u = 1.0 - t;
                double val = u * u * u * p0y + 3.0 * u * u * t * p1y + 3.0 * u * t * t * p2y +
                             t * t * t * p3y;

                addEvent(beat, std::clamp(static_cast<int>(std::round(val)), 0, maxValue));
            }
        }
    }
}

// =============================================================================
// Private Sync Helpers
// =============================================================================

void ClipSynchronizer::syncMidiClipToEngine(ClipId clipId, const ClipInfo* clip) {
    // Get the Tracktion AudioTrack for this MAGDA track
    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("syncClipToEngine: Tracktion track not found for MAGDA track: " << clip->trackId);
        return;
    }

    namespace te = tracktion;
    te::MidiClip* midiClipPtr = nullptr;

    // Check if clip already exists in Tracktion Engine
    {
        juce::ScopedLock lock(clipLock_);
        auto it = clipIdToEngineId_.find(clipId);
        if (it != clipIdToEngineId_.end()) {
            // Clip exists - find it and update
            std::string engineId = it->second;

            // Find the MidiClip in the track
            for (auto* teClip : audioTrack->getClips()) {
                if (teClip->itemID.toString().toStdString() == engineId) {
                    midiClipPtr = dynamic_cast<te::MidiClip*>(teClip);
                    break;
                }
            }

            // Clip not found on expected track — it may have moved.
            // Remove the old TE clip from whichever track still holds it.
            if (!midiClipPtr) {
                DBG("ClipSynchronizer: MIDI clip moved or stale, removing old TE clip " << clipId);
                removeTeClipByEngineId(engineId);
                clipIdToEngineId_.erase(it);
                engineIdToClipId_.erase(engineId);
            }
        }
    }

    // Create clip if it doesn't exist
    if (!midiClipPtr) {
        // Use beats-based positioning via TE's tempo sequence (always correct regardless of tempo)
        auto startPos =
            edit_.tempoSequence.beatsToTime(te::BeatPosition::fromBeats(clip->startBeats));
        auto endPos = edit_.tempoSequence.beatsToTime(
            te::BeatPosition::fromBeats(clip->startBeats + clip->lengthBeats));
        auto timeRange = te::TimeRange(startPos, endPos);

        auto clipRef = audioTrack->insertMIDIClip(timeRange, nullptr);
        if (!clipRef) {
            DBG("syncClipToEngine: Failed to create MIDI clip");
            return;
        }

        midiClipPtr = clipRef.get();

        // Store clip ID mapping (use clip's EditItemID as string)
        std::string engineClipId = midiClipPtr->itemID.toString().toStdString();
        {
            juce::ScopedLock lock(clipLock_);
            clipIdToEngineId_[clipId] = engineClipId;
            engineIdToClipId_[engineClipId] = clipId;
        }
    }

    // Update clip position/length using beats-based positioning via TE's tempo sequence
    // This ensures correct positioning regardless of when TE's tempo is updated
    {
        auto startPos =
            edit_.tempoSequence.beatsToTime(te::BeatPosition::fromBeats(clip->startBeats));
        auto endPos = edit_.tempoSequence.beatsToTime(
            te::BeatPosition::fromBeats(clip->startBeats + clip->lengthBeats));
        midiClipPtr->setStart(startPos, true, false);
        midiClipPtr->setEnd(endPos, false);
    }

    // Set up internal looping on the TE clip
    if (clip->loopEnabled && clip->loopLengthBeats > 0.0) {
        // Use the stored loop region length, not the clip container length
        double loopBeats = clip->loopLengthBeats;
        auto& tempoSeq = edit_.tempoSequence;
        auto loopStartTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(0.0));
        auto loopEndTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(loopBeats));

        midiClipPtr->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
        midiClipPtr->setLoopRangeBeats(
            {te::BeatPosition::fromBeats(0.0), te::BeatPosition::fromBeats(loopBeats)});

        // Set TE offset from midiOffset (beats) so playback starts at the phase position
        double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
        double phaseSeconds = clip->midiOffset * (60.0 / bpm);
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(phaseSeconds));
    } else {
        midiClipPtr->disableLooping();
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));
    }

    // Clear existing notes and rebuild from ClipManager
    auto& sequence = midiClipPtr->getSequence();
    sequence.clear(nullptr);

    // Calculate the beat range visible in this clip
    double clipLengthBeats = clip->lengthBeats;
    double contentLengthBeats = (clip->loopEnabled && clip->loopLengthBeats > 0.0)
                                    ? clip->loopLengthBeats
                                    : clipLengthBeats;
    // For non-looped clips, midiTrimOffset shifts the visible window (left-resize trim)
    double effectiveOffset = (clip->loopEnabled) ? 0.0 : clip->midiTrimOffset;
    double visibleStart = effectiveOffset;
    double visibleEnd = effectiveOffset + contentLengthBeats;

    // Add notes to TE sequence — notes stay at original positions,
    // TE offset + looping handles phase wrapping natively
    for (const auto& note : clip->midiNotes) {
        double noteStart = note.startBeat;
        double noteEnd = noteStart + note.lengthBeats;

        // Skip notes completely outside the visible range
        if (noteEnd <= visibleStart || noteStart >= visibleEnd)
            continue;

        double adjustedLength = note.lengthBeats;

        // Truncate notes at content boundary to prevent stuck notes
        if (noteStart >= contentLengthBeats)
            continue;
        if (noteEnd > contentLengthBeats)
            adjustedLength = contentLengthBeats - noteStart;

        // For non-looped clips with midiOffset, shift note positions
        double adjustedStart = noteStart - effectiveOffset;

        // Truncate note if it starts before the visible range
        if (adjustedStart < 0.0) {
            adjustedLength = noteEnd - visibleStart;
            adjustedStart = 0.0;
        }

        // Truncate note if it extends past the content boundary
        if (adjustedStart + adjustedLength > contentLengthBeats)
            adjustedLength = contentLengthBeats - adjustedStart;

        if (adjustedLength > 0.0) {
            sequence.addNote(note.noteNumber, te::BeatPosition::fromBeats(adjustedStart),
                             te::BeatDuration::fromBeats(adjustedLength), note.velocity, 0,
                             nullptr);
        }
    }

    // Add CC events with interpolation (grouped by controller number)
    {
        std::map<int, std::vector<MidiCCData>> ccByController;
        for (const auto& cc : clip->midiCCData)
            ccByController[cc.controller].push_back(cc);

        for (const auto& [ccNum, ccEvents] : ccByController) {
            interpolateCCEvents(sequence, ccEvents, ccNum, effectiveOffset, visibleStart,
                                visibleEnd, contentLengthBeats);
        }
    }

    // Add pitch bend events with interpolation
    interpolateCCEvents(sequence, clip->midiPitchBendData, te::MidiControllerEvent::pitchWheelType,
                        effectiveOffset, visibleStart, visibleEnd, contentLengthBeats);
}

void ClipSynchronizer::syncAudioClipToEngine(ClipId clipId, const ClipInfo* clip) {
    namespace te = tracktion;

    // 1. Get Tracktion track
    auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("ClipSynchronizer: Track not found for audio clip " << clipId);
        return;
    }

    // 2. Check if clip already synced
    te::WaveAudioClip* audioClipPtr = nullptr;
    {
        juce::ScopedLock lock(clipLock_);
        auto it = clipIdToEngineId_.find(clipId);

        if (it != clipIdToEngineId_.end()) {
            // UPDATE existing clip
            std::string engineId = it->second;

            // Find clip in track by engine ID
            for (auto* teClip : audioTrack->getClips()) {
                if (teClip->itemID.toString().toStdString() == engineId) {
                    audioClipPtr = dynamic_cast<te::WaveAudioClip*>(teClip);
                    break;
                }
            }

            // Clip not found on expected track — it may have moved.
            // Remove the old TE clip from whichever track still holds it.
            if (!audioClipPtr) {
                DBG("ClipSynchronizer: Clip moved or stale, removing old TE clip " << clipId);
                removeTeClipByEngineId(engineId);
                clipIdToEngineId_.erase(it);
                engineIdToClipId_.erase(engineId);
            }
        }
    }

    // 3. CREATE new clip if doesn't exist
    if (!audioClipPtr) {
        if (clip->audioFilePath.isEmpty()) {
            DBG("ClipSynchronizer: No audio file for clip " << clipId);
            return;
        }
        juce::File audioFile(clip->audioFilePath);
        if (!audioFile.existsAsFile()) {
            DBG("ClipSynchronizer: Audio file not found: " << clip->audioFilePath);
            return;
        }

        double createStart = clip->startTime;
        double createEnd = createStart + clip->length;
        auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(createStart),
                                       te::TimePosition::fromSeconds(createEnd));

        auto clipRef =
            insertWaveClip(*audioTrack, audioFile.getFileNameWithoutExtension(), audioFile,
                           te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

        if (!clipRef) {
            DBG("ClipSynchronizer: Failed to create WaveAudioClip");
            return;
        }

        audioClipPtr = clipRef.get();

        // Set timestretcher mode at creation time
        // When timeStretchMode is 0 (disabled), keep it disabled — TE's
        // getActualTimeStretchMode() will auto-upgrade to defaultMode when
        // autoPitch/autoTempo/pitchChange require it.
        // Force defaultMode when speedRatio != 1.0 or warp is enabled.
        // Analog pitch: force disabled mode (pure resampling via speedRatio).
        {
            bool isAnalog = clip->isAnalogPitchActive();
            auto stretchMode = static_cast<te::TimeStretcher::Mode>(clip->timeStretchMode);
            if (!isAnalog && stretchMode == te::TimeStretcher::disabled &&
                (std::abs(clip->speedRatio - 1.0) > 0.001 || clip->warpEnabled))
                stretchMode = te::TimeStretcher::defaultMode;
            if (isAnalog)
                stretchMode = te::TimeStretcher::disabled;
            audioClipPtr->setTimeStretchMode(stretchMode);
        }
        audioClipPtr->setUsesProxy(false);

        // Populate source file metadata from TE's loopInfo
        {
            auto& loopInfoRef = audioClipPtr->getLoopInfo();
            auto waveInfo = audioClipPtr->getWaveInfo();
            if (auto* mutableClip = ClipManager::getInstance().getClip(clipId))
                mutableClip->setSourceMetadata(loopInfoRef.getNumBeats(),
                                               loopInfoRef.getBpm(waveInfo));
        }

        // Store bidirectional mapping
        std::string engineClipId = audioClipPtr->itemID.toString().toStdString();
        {
            juce::ScopedLock lock(clipLock_);
            clipIdToEngineId_[clipId] = engineClipId;
            engineIdToClipId_[engineClipId] = clipId;
        }

        DBG("ClipSynchronizer: Created WaveAudioClip (engine ID: " << engineClipId << ")");
    }

    // 3b. REVERSE — must be handled before position/loop/offset sync.
    // setIsReversed triggers updateReversedState() which:
    //   1. Points source to the original file
    //   2. Starts async render of reversed proxy (if reversing)
    //   3. Calls reverseLoopPoints() to transform offset/loop range
    //   4. Calls changed() which updates thumbnails
    // We MUST return after this — the subsequent sync steps would overwrite
    // TE's reversed offset/loop with our model's pre-reverse values.
    // The playback graph rebuild is deferred until the proxy file is ready.
    if (clip->isReversed != audioClipPtr->getIsReversed()) {
        audioClipPtr->setIsReversed(clip->isReversed);

        // Read back ALL of TE's transformed values into our model
        if (auto* mutableClip = ClipManager::getInstance().getClip(clipId)) {
            double teOffset = audioClipPtr->getPosition().getOffset().inSeconds();
            mutableClip->offset = teOffset;
            if (mutableClip->loopEnabled) {
                mutableClip->loopStart = audioClipPtr->getLoopStart().inSeconds();
                mutableClip->loopLength = audioClipPtr->getLoopLength().inSeconds();
            } else {
                mutableClip->loopStart = teOffset;
            }
        }

        // Check if the reversed proxy file is ready
        auto playbackFile = audioClipPtr->getPlaybackFile();
        if (playbackFile.getFile().existsAsFile()) {
            if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                ctx->reallocate();
                if (onGraphReallocated)
                    onGraphReallocated();
            }
        } else {
            pendingReverseClipId_ = clipId;
        }

        return;  // Don't let subsequent sync steps overwrite TE's reversed state
    }

    // 4. UPDATE clip position/length
    // Read seconds directly — BPM handler keeps these in sync for autoTempo clips.
    double engineStart = clip->startTime;
    double engineEnd = clip->startTime + clip->length;

    auto currentPos = audioClipPtr->getPosition();
    auto currentStart = currentPos.getStart().inSeconds();
    auto currentEnd = currentPos.getEnd().inSeconds();

    // Use setPosition() to update start and length atomically (reduces audio glitches)
    bool needsPositionUpdate =
        std::abs(currentStart - engineStart) > 0.001 || std::abs(currentEnd - engineEnd) > 0.001;

    if (needsPositionUpdate) {
        auto newTimeRange = te::TimeRange(te::TimePosition::fromSeconds(engineStart),
                                          te::TimePosition::fromSeconds(engineEnd));
        audioClipPtr->setPosition(te::ClipPosition{newTimeRange, currentPos.getOffset()});
    }

    // 5. UPDATE speed ratio and auto-tempo mode
    // Handle auto-tempo (musical mode) vs time-based mode
    if (clip->isBeatsAuthoritative()) {
        // ========================================================================
        // AUTO-TEMPO MODE (Beat-based length, maintains musical time)
        // Warp also uses this path — TE only passes warpMap to WaveNodeRealTime
        // via the auto-tempo code path in EditNodeBuilder.
        // ========================================================================
        // In auto-tempo mode:
        // - TE's autoTempo is enabled (clips stretch/shrink with BPM)
        // - speedRatio must be 1.0 (TE requirement)
        // - Use beat-based loop range (setLoopRangeBeats)

        // Enable auto-tempo in TE if not already enabled
        if (!audioClipPtr->getAutoTempo()) {
            audioClipPtr->setAutoTempo(true);
        }

        // Force speedRatio to 1.0 (auto-tempo requirement)
        if (std::abs(audioClipPtr->getSpeedRatio() - 1.0) > 0.001) {
            audioClipPtr->setSpeedRatio(1.0);
        }

        // Auto-tempo requires a valid stretch mode for TE to time-stretch audio
        if (audioClipPtr->getTimeStretchMode() == te::TimeStretcher::disabled) {
            audioClipPtr->setTimeStretchMode(te::TimeStretcher::defaultMode);
        }

    } else {
        // ========================================================================
        // TIME-BASED MODE (Fixed absolute time, current default behavior)
        // ========================================================================

        // Always disable autoTempo in TE when our model says it's off
        if (audioClipPtr->getAutoTempo()) {
            audioClipPtr->setAutoTempo(false);
        }

        double teSpeedRatio = clip->speedRatio;
        double currentSpeedRatio = audioClipPtr->getSpeedRatio();

        // Sync time stretch mode — warp also requires a valid stretcher
        auto desiredMode = static_cast<te::TimeStretcher::Mode>(clip->timeStretchMode);
        bool isAnalog = clip->isAnalogPitchActive();
        if (!isAnalog && desiredMode == te::TimeStretcher::disabled &&
            (std::abs(teSpeedRatio - 1.0) > 0.001 || clip->warpEnabled))
            desiredMode = te::TimeStretcher::defaultMode;
        // When analog: force disabled mode (pure resampling)
        if (isAnalog)
            desiredMode = te::TimeStretcher::disabled;
        if (audioClipPtr->getTimeStretchMode() != desiredMode) {
            audioClipPtr->setTimeStretchMode(desiredMode);
        }

        if (std::abs(currentSpeedRatio - teSpeedRatio) > 0.001) {
            audioClipPtr->setUsesProxy(false);
            audioClipPtr->setSpeedRatio(teSpeedRatio);
        }

        // Sync warp state to engine (time-based warp — rare, but handle it)
        if (clip->warpEnabled != audioClipPtr->getWarpTime()) {
            audioClipPtr->setWarpTime(clip->warpEnabled);
        }
    }

    // 5b. WARP — sync warp state and restore markers (applies to both code paths)
    if (clip->warpEnabled) {
        if (!audioClipPtr->getWarpTime()) {
            audioClipPtr->setWarpTime(true);
        }

        // Restore saved warp markers if TE has no user markers yet
        if (!clip->warpMarkers.empty()) {
            auto& warpManager = audioClipPtr->getWarpTimeManager();
            auto existingMarkers = warpManager.getMarkers();
            // TE creates 2 default boundary markers; if only those exist, restore saved
            if (existingMarkers.size() <= 2) {
                warpManager.removeAllMarkers();
                for (const auto& wm : clip->warpMarkers) {
                    warpManager.insertMarker(
                        te::WarpMarker(te::TimePosition::fromSeconds(wm.sourceTime),
                                       te::TimePosition::fromSeconds(wm.warpTime)));
                }
                DBG("ClipSynchronizer: Restored " << clip->warpMarkers.size()
                                                  << " warp markers for clip " << clipId);
            }
        }
    }

    // 6. UPDATE loop properties (BEFORE offset — setLoopRangeBeats can reset offset)
    // Use beat-based loop range in auto-tempo/warp mode, time-based otherwise
    if (clip->isBeatsAuthoritative()) {
        // Auto-tempo mode: ALWAYS set beat-based loop range
        // The loop range defines the clip's musical extent (not just the loop region)

        // Get tempo for beat calculations
        double bpm = edit_.tempoSequence.getTempo(0)->getBpm();

        // Override TE's loopInfo BPM to match our calibrated sourceBPM.
        // setAutoTempo calibrates sourceBPM = projectBPM / speedRatio so that
        // enabling autoTempo doesn't change playback speed.  TE uses loopInfo
        // to map source beats ↔ source time, so the two must agree.
        if (clip->sourceBPM > 0.0) {
            auto waveInfo = audioClipPtr->getWaveInfo();
            auto& li = audioClipPtr->getLoopInfo();
            double currentLoopInfoBpm = li.getBpm(waveInfo);
            if (std::abs(currentLoopInfoBpm - clip->sourceBPM) > 0.1) {
                li.setBpm(clip->sourceBPM, waveInfo);
            }
        }

        // Calculate beat range using centralized helper
        auto [loopStartBeats, loopLengthBeats] = ClipOperations::getAutoTempoBeatRange(*clip, bpm);

        // Set the beat-based loop range in TE
        auto loopRange = te::BeatRange(te::BeatPosition::fromBeats(loopStartBeats),
                                       te::BeatDuration::fromBeats(loopLengthBeats));
        audioClipPtr->setLoopRangeBeats(loopRange);
    } else {
        // Time-based mode: Use time-based loop range
        // Only use setLoopRange (time-based), NOT setLoopRangeBeats which forces
        // autoTempo=true and speedRatio=1.0, breaking time-stretch.
        if (clip->loopEnabled && clip->getSourceLength() > 0.0) {
            auto loopStartTime = te::TimePosition::fromSeconds(clip->getTeLoopStart());
            auto loopEndTime = te::TimePosition::fromSeconds(clip->getTeLoopEnd());
            audioClipPtr->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
        } else if (audioClipPtr->isLooping()) {
            audioClipPtr->setLoopRange({});
        }
    }

    // 7. UPDATE audio offset (trim point in file)
    // Must come AFTER loop range — setLoopRangeBeats resets offset internally
    {
        double projectBpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
        double teOffset = juce::jmax(0.0, clip->getTeOffset(clip->loopEnabled, projectBpm));
        auto currentOffset = audioClipPtr->getPosition().getOffset().inSeconds();
        if (std::abs(currentOffset - teOffset) > 0.001) {
            audioClipPtr->setOffset(te::TimeDuration::fromSeconds(teOffset));
        }
    }

    // 8. PITCH
    {
        bool isAnalog = clip->isAnalogPitchActive();
        if (clip->autoPitch != audioClipPtr->getAutoPitch())
            audioClipPtr->setAutoPitch(isAnalog ? false : clip->autoPitch);
        if (static_cast<int>(audioClipPtr->getAutoPitchMode()) != clip->autoPitchMode)
            audioClipPtr->setAutoPitchMode(
                static_cast<te::AudioClipBase::AutoPitchMode>(clip->autoPitchMode));
        if (isAnalog) {
            if (std::abs(audioClipPtr->getPitchChange()) > 0.001f)
                audioClipPtr->setPitchChange(0.0f);
        } else {
            if (std::abs(audioClipPtr->getPitchChange() - clip->pitchChange) > 0.001f)
                audioClipPtr->setPitchChange(clip->pitchChange);
        }
        if (audioClipPtr->getTransposeSemiTones(false) != clip->transpose)
            audioClipPtr->setTranspose(clip->transpose);
    }

    // 9. BEAT DETECTION
    if (clip->autoDetectBeats != audioClipPtr->getAutoDetectBeats())
        audioClipPtr->setAutoDetectBeats(clip->autoDetectBeats);
    if (std::abs(audioClipPtr->getBeatSensitivity() - clip->beatSensitivity) > 0.001f)
        audioClipPtr->setBeatSensitivity(clip->beatSensitivity);

    // 10. PLAYBACK (isReversed handled at top of function)

    // 11. PER-CLIP MIX
    {
        float combinedGain = clip->volumeDB + clip->gainDB;
        if (std::abs(audioClipPtr->getGainDB() - combinedGain) > 0.001f)
            audioClipPtr->setGainDB(combinedGain);
    }
    if (std::abs(audioClipPtr->getPan() - clip->pan) > 0.001f)
        audioClipPtr->setPan(clip->pan);

    // 12. FADES
    {
        double teFadeIn = audioClipPtr->getFadeIn().inSeconds();
        if (std::abs(teFadeIn - clip->fadeIn) > 0.001)
            audioClipPtr->setFadeIn(te::TimeDuration::fromSeconds(clip->fadeIn));
    }
    {
        double teFadeOut = audioClipPtr->getFadeOut().inSeconds();
        if (std::abs(teFadeOut - clip->fadeOut) > 0.001)
            audioClipPtr->setFadeOut(te::TimeDuration::fromSeconds(clip->fadeOut));
    }
    if (static_cast<int>(audioClipPtr->getFadeInType()) != clip->fadeInType)
        audioClipPtr->setFadeInType(static_cast<te::AudioFadeCurve::Type>(clip->fadeInType));
    if (static_cast<int>(audioClipPtr->getFadeOutType()) != clip->fadeOutType)
        audioClipPtr->setFadeOutType(static_cast<te::AudioFadeCurve::Type>(clip->fadeOutType));
    if (static_cast<int>(audioClipPtr->getFadeInBehaviour()) != clip->fadeInBehaviour)
        audioClipPtr->setFadeInBehaviour(
            static_cast<te::AudioClipBase::FadeBehaviour>(clip->fadeInBehaviour));
    if (static_cast<int>(audioClipPtr->getFadeOutBehaviour()) != clip->fadeOutBehaviour)
        audioClipPtr->setFadeOutBehaviour(
            static_cast<te::AudioClipBase::FadeBehaviour>(clip->fadeOutBehaviour));
    if (clip->autoCrossfade != audioClipPtr->getAutoCrossfade())
        audioClipPtr->setAutoCrossfade(clip->autoCrossfade);

    // 13. CHANNELS — removed (L/R controls removed from Inspector)
}

}  // namespace magda
