#include "ClipSynchronizer.hpp"

#include <iostream>
#include <unordered_set>

#include "../core/ClipManager.hpp"
#include "../core/ClipOperations.hpp"
#include "TrackController.hpp"
#include "WarpMarkerManager.hpp"

namespace magda {

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
    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);
}

ClipSynchronizer::~ClipSynchronizer() {
    // Unregister from ClipManager
    ClipManager::getInstance().removeListener(this);
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
        if (auto* ctx = edit_.getCurrentPlaybackContext()) {
            ctx->reallocate();
        }
    }
}

void ClipSynchronizer::clipPropertyChanged(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        DBG("ClipSynchronizer::clipPropertyChanged: clip " << clipId
                                                           << " not found in ClipManager");
        return;
    }
    DBG("[CLIP-SYNC-PROP-CHANGED] clipId="
        << clipId << " view=" << (int)clip->view << " startTime=" << clip->startTime << " length="
        << clip->length << " offset=" << clip->offset << " loopStart=" << clip->loopStart
        << " getTeOffset()=" << clip->getTeOffset(clip->loopEnabled));

    if (clip->autoTempo || clip->warpEnabled) {
        DBG("[CLIP-SYNCHRONIZER] clipPropertyChanged clip "
            << clipId << " length=" << clip->length << " loopLength=" << clip->loopLength
            << " loopLengthBeats=" << clip->loopLengthBeats << " lengthBeats=" << clip->lengthBeats
            << " startTime=" << clip->startTime << " startBeats=" << clip->startBeats);
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
                }
            } else {
                // Clip already synced — propagate property changes to TE clip
                auto* teClip = getSessionTeClip(clipId);
                if (teClip) {
                    // Update clip length
                    teClip->setLength(te::TimeDuration::fromSeconds(clip->length), false);

                    // Update launch quantization
                    auto* lq = teClip->getLaunchQuantisation();
                    if (lq) {
                        lq->type = toTELaunchQType(clip->launchQuantize);
                    }

                    // Update clip's own loop state
                    if (clip->loopEnabled) {
                        if (clip->getSourceLength() > 0.0) {
                            teClip->setLoopRange(
                                te::TimeRange(te::TimePosition::fromSeconds(clip->getTeLoopStart()),
                                              te::TimePosition::fromSeconds(clip->getTeLoopEnd())));
                        }
                    } else {
                        teClip->disableLooping();
                    }

                    // Update looping on the launch handle
                    auto launchHandle = teClip->getLaunchHandle();
                    if (launchHandle) {
                        if (clip->loopEnabled) {
                            double loopLengthSeconds = clip->getSourceLength() / clip->speedRatio;
                            double bps = edit_.tempoSequence.getBpmAt(te::TimePosition()) / 60.0;
                            double loopLengthBeats = loopLengthSeconds * bps;
                            launchHandle->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
                        } else {
                            launchHandle->setLooping(std::nullopt);
                        }
                    }

                    // Sync session-applicable audio clip properties
                    if (clip->type == ClipType::Audio) {
                        auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip);
                        if (audioClip) {
                            // Pitch
                            if (clip->autoPitch != audioClip->getAutoPitch())
                                audioClip->setAutoPitch(clip->autoPitch);
                            if (std::abs(audioClip->getPitchChange() - clip->pitchChange) > 0.001f)
                                audioClip->setPitchChange(clip->pitchChange);
                            if (audioClip->getTransposeSemiTones(false) != clip->transpose)
                                audioClip->setTranspose(clip->transpose);
                            // Playback
                            if (clip->isReversed != audioClip->getIsReversed())
                                audioClip->setIsReversed(clip->isReversed);
                            // Per-Clip Mix
                            if (std::abs(audioClip->getGainDB() - clip->gainDB) > 0.001f)
                                audioClip->setGainDB(clip->gainDB);
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
                }
            }
        }
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

        // Set timestretcher mode — keep disabled when mode is 0 and speedRatio is 1.0
        // Warp also requires a valid stretcher
        auto stretchMode = static_cast<te::TimeStretcher::Mode>(clip->timeStretchMode);
        if (stretchMode == te::TimeStretcher::disabled &&
            (std::abs(clip->speedRatio - 1.0) > 0.001 || clip->warpEnabled))
            stretchMode = te::TimeStretcher::defaultMode;
        audioClipPtr->setTimeStretchMode(stretchMode);

        // Set speed ratio (BEFORE offset, since TE offset
        // is in stretched time and must be set after speed ratio)
        if (std::abs(clip->speedRatio - 1.0) > 0.001) {
            if (audioClipPtr->getAutoTempo()) {
                audioClipPtr->setAutoTempo(false);
            }
            audioClipPtr->setSpeedRatio(clip->speedRatio);
        }

        // Set file offset (trim point) - relative to loop start, in stretched time
        audioClipPtr->setOffset(
            te::TimeDuration::fromSeconds(clip->getTeOffset(clip->loopEnabled)));

        // Set looping properties
        if (clip->loopEnabled && clip->getSourceLength() > 0.0) {
            audioClipPtr->setLoopRange(
                te::TimeRange(te::TimePosition::fromSeconds(clip->getTeLoopStart()),
                              te::TimePosition::fromSeconds(clip->getTeLoopEnd())));
        }

        // Set per-clip launch quantization
        audioClipPtr->setUsesGlobalLaunchQuatisation(false);
        if (auto* lq = audioClipPtr->getLaunchQuantisation()) {
            lq->type = toTELaunchQType(clip->launchQuantize);
        }

        // Sync session-applicable audio properties at creation
        if (clip->autoPitch)
            audioClipPtr->setAutoPitch(true);
        if (std::abs(clip->pitchChange) > 0.001f)
            audioClipPtr->setPitchChange(clip->pitchChange);
        if (clip->transpose != 0)
            audioClipPtr->setTranspose(clip->transpose);
        if (clip->isReversed)
            audioClipPtr->setIsReversed(true);
        if (std::abs(clip->gainDB) > 0.001f)
            audioClipPtr->setGainDB(clip->gainDB);
        if (std::abs(clip->pan) > 0.001f)
            audioClipPtr->setPan(clip->pan);

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

        return true;
    }

    return false;
}

void ClipSynchronizer::removeSessionClipFromSlot(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (teClip)
        teClip->removeFromParent();
}

void ClipSynchronizer::launchSessionClip(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip) {
        DBG("ClipSynchronizer::launchSessionClip: TE clip not found for clip " << clipId);
        return;
    }

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle) {
        DBG("ClipSynchronizer::launchSessionClip: No LaunchHandle for clip " << clipId);
        return;
    }

    // Set looping before play
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        if (clip->loopEnabled) {
            double srcLength = clip->getSourceLength();
            if (clip->type == ClipType::Audio && srcLength > 0.0) {
                teClip->setLoopRange(
                    te::TimeRange(te::TimePosition::fromSeconds(clip->getTeLoopStart()),
                                  te::TimePosition::fromSeconds(clip->getTeLoopEnd())));
                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                double loopDurationBeats = (srcLength / clip->speedRatio) * (bpm / 60.0);
                launchHandle->setLooping(te::BeatDuration::fromBeats(loopDurationBeats));
            } else {
                // MIDI: convert source region to beats
                double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
                double loopStartBeat = clip->loopStart * (bpm / 60.0);
                double loopLengthBeats = srcLength * (bpm / 60.0);
                double loopEndBeat = loopStartBeat + loopLengthBeats;

                auto& tempoSeq = edit_.tempoSequence;
                auto loopStartTime =
                    tempoSeq.beatsToTime(te::BeatPosition::fromBeats(loopStartBeat));
                auto loopEndTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(loopEndBeat));
                teClip->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
                teClip->setLoopRangeBeats({te::BeatPosition::fromBeats(loopStartBeat),
                                           te::BeatPosition::fromBeats(loopEndBeat)});

                launchHandle->setLooping(te::BeatDuration::fromBeats(loopLengthBeats));
            }
        } else {
            teClip->disableLooping();
            launchHandle->setLooping(std::nullopt);
        }
    }

    launchHandle->play(std::nullopt);
}

void ClipSynchronizer::stopSessionClip(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip)
        return;

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle)
        return;

    launchHandle->stop(std::nullopt);

    // Reset synth plugins on the clip's track to prevent stuck notes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip && clip->type == ClipType::MIDI) {
        auto* audioTrack = trackController_.getAudioTrack(clip->trackId);
        if (audioTrack) {
            for (auto* plugin : audioTrack->pluginList) {
                if (plugin->isSynth()) {
                    plugin->reset();
                }
            }
        }
    }
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
// Warp Marker Operations (Delegated to WarpMarkerManager)
// =============================================================================

bool ClipSynchronizer::getTransientTimes(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    return warpMarkerManager_.getTransientTimes(edit_, clipIdToEngineId_, clipId);
}

void ClipSynchronizer::enableWarp(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    warpMarkerManager_.enableWarp(edit_, clipIdToEngineId_, clipId);
}

void ClipSynchronizer::disableWarp(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    warpMarkerManager_.disableWarp(edit_, clipIdToEngineId_, clipId);
}

std::vector<WarpMarkerInfo> ClipSynchronizer::getWarpMarkers(ClipId clipId) {
    juce::ScopedLock lock(clipLock_);
    return warpMarkerManager_.getWarpMarkers(edit_, clipIdToEngineId_, clipId);
}

int ClipSynchronizer::addWarpMarker(ClipId clipId, double sourceTime, double warpTime) {
    juce::ScopedLock lock(clipLock_);
    return warpMarkerManager_.addWarpMarker(edit_, clipIdToEngineId_, clipId, sourceTime, warpTime);
}

double ClipSynchronizer::moveWarpMarker(ClipId clipId, int index, double newWarpTime) {
    juce::ScopedLock lock(clipLock_);
    return warpMarkerManager_.moveWarpMarker(edit_, clipIdToEngineId_, clipId, index, newWarpTime);
}

void ClipSynchronizer::removeWarpMarker(ClipId clipId, int index) {
    juce::ScopedLock lock(clipLock_);
    warpMarkerManager_.removeWarpMarker(edit_, clipIdToEngineId_, clipId, index);
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

            if (!midiClipPtr) {
                // Clear stale mapping and recreate
                clipIdToEngineId_.erase(it);
                engineIdToClipId_.erase(engineId);
            }
        }
    }

    // Create clip if it doesn't exist
    if (!midiClipPtr) {
        auto timeRange =
            te::TimeRange(te::TimePosition::fromSeconds(clip->startTime),
                          te::TimePosition::fromSeconds(clip->startTime + clip->length));

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

    // Update clip position/length
    // CRITICAL: Use preserveSync=true to maintain the content offset
    // When false, Tracktion adjusts the content offset which breaks note playback
    midiClipPtr->setStart(te::TimePosition::fromSeconds(clip->startTime), true, false);
    midiClipPtr->setEnd(te::TimePosition::fromSeconds(clip->startTime + clip->length), false);

    // Force offset to 0 — note shifting is handled manually below
    midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

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
    } else {
        midiClipPtr->disableLooping();
    }

    // Clear existing notes and rebuild from ClipManager
    auto& sequence = midiClipPtr->getSequence();
    sequence.clear(nullptr);

    // Calculate the beat range visible in this clip based on midiOffset
    const double beatsPerSecond = edit_.tempoSequence.getBpmAt(te::TimePosition()) / 60.0;
    double clipLengthBeats = clip->length * beatsPerSecond;
    // When looping, notes only span the loop region — TE handles repetition
    double contentLengthBeats = (clip->loopEnabled && clip->loopLengthBeats > 0.0)
                                    ? clip->loopLengthBeats
                                    : clipLengthBeats;
    // Apply midiOffset in all modes so arrangement clips also respect the offset
    double effectiveOffset = clip->midiOffset;
    double visibleStart = effectiveOffset;  // Where the clip's "view window" starts
    double visibleEnd = effectiveOffset + contentLengthBeats;

    DBG("MIDI SYNC clip " << clipId << ":");
    DBG("  midiOffset=" << clip->midiOffset << ", clipLength=" << clipLengthBeats << " beats");
    DBG("  loopEnabled=" << (int)clip->loopEnabled
                         << ", loopLengthBeats=" << clip->loopLengthBeats);
    DBG("  contentLengthBeats=" << contentLengthBeats);
    DBG("  Visible range: [" << visibleStart << ", " << visibleEnd << ")");
    DBG("  Total notes: " << clip->midiNotes.size());

    // Only add notes that overlap with the visible range
    int addedCount = 0;

    for (const auto& note : clip->midiNotes) {
        double noteStart = note.startBeat;
        double noteEnd = noteStart + note.lengthBeats;

        // Skip notes completely outside the visible range
        if (noteEnd <= visibleStart || noteStart >= visibleEnd) {
            continue;
        }

        // Truncate notes at content boundary to prevent stuck notes
        double adjustedLength = note.lengthBeats;
        if (noteStart >= contentLengthBeats)
            continue;
        if (noteEnd > contentLengthBeats)
            adjustedLength = contentLengthBeats - noteStart;

        // Calculate position relative to clip start (subtract midiOffset for session clips only)
        double adjustedStart = noteStart - effectiveOffset;

        // Truncate note if it starts before the visible range
        if (adjustedStart < 0.0) {
            adjustedLength = noteEnd - visibleStart;
            adjustedStart = 0.0;
        }

        // Truncate note if it extends past the content boundary
        if (adjustedStart + adjustedLength > contentLengthBeats) {
            adjustedLength = contentLengthBeats - adjustedStart;
        }

        // Add note to Tracktion (all positions are now non-negative)
        if (adjustedLength > 0.0) {
            sequence.addNote(note.noteNumber, te::BeatPosition::fromBeats(adjustedStart),
                             te::BeatDuration::fromBeats(adjustedLength), note.velocity, 0,
                             nullptr);
            addedCount++;
        }
    }

    DBG("  Added " << addedCount << " notes to Tracktion");
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

            // If mapping is stale, clear it
            if (!audioClipPtr) {
                DBG("ClipSynchronizer: Clip mapping stale, recreating for clip " << clipId);
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
        auto stretchMode = static_cast<te::TimeStretcher::Mode>(clip->timeStretchMode);
        if (stretchMode == te::TimeStretcher::disabled &&
            (std::abs(clip->speedRatio - 1.0) > 0.001 || clip->warpEnabled))
            stretchMode = te::TimeStretcher::defaultMode;
        audioClipPtr->setTimeStretchMode(stretchMode);
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
        DBG("========== REVERSE TOGGLE [" << clipId << "] ==========");
        DBG("  Setting isReversed = " << (int)clip->isReversed);
        DBG("  BEFORE setIsReversed:");
        DBG("    TE offset: " << audioClipPtr->getPosition().getOffset().inSeconds());
        DBG("    TE loopStart: " << audioClipPtr->getLoopStart().inSeconds());
        DBG("    TE loopLength: " << audioClipPtr->getLoopLength().inSeconds());
        DBG("    TE isLooping: " << (int)audioClipPtr->isLooping());
        DBG("    TE sourceFile: " << audioClipPtr->getCurrentSourceFile().getFullPathName());
        DBG("    TE playbackFile: " << audioClipPtr->getPlaybackFile().getFile().getFullPathName());
        DBG("    TE speedRatio: " << audioClipPtr->getSpeedRatio());
        DBG("    Model offset: " << clip->offset);
        DBG("    Model loopStart: " << clip->loopStart);
        DBG("    Model loopLength: " << clip->loopLength);
        DBG("    Model loopEnabled: " << (int)clip->loopEnabled);

        audioClipPtr->setIsReversed(clip->isReversed);

        DBG("  AFTER setIsReversed:");
        DBG("    TE offset: " << audioClipPtr->getPosition().getOffset().inSeconds());
        DBG("    TE loopStart: " << audioClipPtr->getLoopStart().inSeconds());
        DBG("    TE loopLength: " << audioClipPtr->getLoopLength().inSeconds());
        DBG("    TE isLooping: " << (int)audioClipPtr->isLooping());
        DBG("    TE sourceFile: " << audioClipPtr->getCurrentSourceFile().getFullPathName());
        DBG("    TE playbackFile: " << audioClipPtr->getPlaybackFile().getFile().getFullPathName());
        DBG("    TE playbackFile exists: "
            << (int)audioClipPtr->getPlaybackFile().getFile().existsAsFile());
        DBG("    TE position: " << audioClipPtr->getPosition().getStart().inSeconds() << " - "
                                << audioClipPtr->getPosition().getEnd().inSeconds());

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
            DBG("  Model UPDATED:");
            DBG("    offset: " << mutableClip->offset);
            DBG("    loopStart: " << mutableClip->loopStart);
            DBG("    loopLength: " << mutableClip->loopLength);
        }

        // Check if the reversed proxy file is ready
        auto playbackFile = audioClipPtr->getPlaybackFile();
        if (playbackFile.getFile().existsAsFile()) {
            DBG("  Proxy file EXISTS — reallocating immediately");
            if (auto* ctx = edit_.getCurrentPlaybackContext())
                ctx->reallocate();
        } else {
            DBG("  Proxy file NOT FOUND — polling until ready (clipId=" << clipId << ")");
            pendingReverseClipId_ = clipId;
        }

        DBG("========== REVERSE TOGGLE DONE ==========");
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
    DBG("========== AUTO-TEMPO SYNC [" << clipId << "] ==========");
    DBG("  OUR MODEL:");
    DBG("    autoTempo: " << (int)clip->autoTempo);
    DBG("    loopEnabled: " << (int)clip->loopEnabled);
    DBG("    loopStartBeats: " << clip->loopStartBeats);
    DBG("    loopLengthBeats: " << clip->loopLengthBeats);
    DBG("    loopStart: " << clip->loopStart);
    DBG("    loopLength: " << clip->loopLength);
    DBG("    offset: " << clip->offset);
    DBG("    length: " << clip->length);
    DBG("    speedRatio: " << clip->speedRatio);
    DBG("    sourceBPM: " << clip->sourceBPM);
    DBG("    sourceNumBeats: " << clip->sourceNumBeats);
    DBG("    getTeOffset(): " << clip->getTeOffset(clip->loopEnabled));
    DBG("    loopStart+loopLength: " << (clip->loopStart + clip->loopLength));
    DBG("  TE STATE BEFORE:");
    DBG("    autoTempo: " << (int)audioClipPtr->getAutoTempo());
    DBG("    isLooping: " << (int)audioClipPtr->isLooping());
    DBG("    loopStartBeats: " << audioClipPtr->getLoopStartBeats().inBeats());
    DBG("    loopLengthBeats: " << audioClipPtr->getLoopLengthBeats().inBeats());
    DBG("    loopStart: " << audioClipPtr->getLoopStart().inSeconds());
    DBG("    loopLength: " << audioClipPtr->getLoopLength().inSeconds());
    DBG("    offset: " << audioClipPtr->getPosition().getOffset().inSeconds());
    DBG("    speedRatio: " << audioClipPtr->getSpeedRatio());

    if (clip->autoTempo || clip->warpEnabled) {
        // ========================================================================
        // AUTO-TEMPO MODE (Beat-based length, maintains musical time)
        // Warp also uses this path — TE only passes warpMap to WaveNodeRealTime
        // via the auto-tempo code path in EditNodeBuilder.
        // ========================================================================
        // In auto-tempo mode:
        // - TE's autoTempo is enabled (clips stretch/shrink with BPM)
        // - speedRatio must be 1.0 (TE requirement)
        // - Use beat-based loop range (setLoopRangeBeats)

        DBG("syncAudioClip [" << clipId << "] ENABLING AUTO-TEMPO MODE");

        // Enable auto-tempo in TE if not already enabled
        if (!audioClipPtr->getAutoTempo()) {
            DBG("  -> Setting TE autoTempo = true");
            audioClipPtr->setAutoTempo(true);
            DBG("  TE STATE AFTER setAutoTempo(true):");
            DBG("    isLooping: " << (int)audioClipPtr->isLooping());
            DBG("    loopStartBeats: " << audioClipPtr->getLoopStartBeats().inBeats());
            DBG("    loopLengthBeats: " << audioClipPtr->getLoopLengthBeats().inBeats());
            DBG("    loopStart: " << audioClipPtr->getLoopStart().inSeconds());
            DBG("    loopLength: " << audioClipPtr->getLoopLength().inSeconds());
            DBG("    offset: " << audioClipPtr->getPosition().getOffset().inSeconds());
        } else {
            DBG("  -> TE autoTempo already true");
        }

        // Force speedRatio to 1.0 (auto-tempo requirement)
        if (std::abs(audioClipPtr->getSpeedRatio() - 1.0) > 0.001) {
            DBG("  -> Forcing speedRatio to 1.0 (was " << audioClipPtr->getSpeedRatio() << ")");
            audioClipPtr->setSpeedRatio(1.0);
        }

        // Auto-tempo requires a valid stretch mode for TE to time-stretch audio
        if (audioClipPtr->getTimeStretchMode() == te::TimeStretcher::disabled) {
            DBG("  -> Setting stretch mode to default (required for autoTempo)");
            audioClipPtr->setTimeStretchMode(te::TimeStretcher::defaultMode);
        }

    } else {
        // ========================================================================
        // TIME-BASED MODE (Fixed absolute time, current default behavior)
        // ========================================================================

        // Always disable autoTempo in TE when our model says it's off
        if (audioClipPtr->getAutoTempo()) {
            DBG("syncAudioClip [" << clipId << "] disabling TE autoTempo");
            audioClipPtr->setAutoTempo(false);
            DBG("  TE STATE AFTER setAutoTempo(false):");
            DBG("    isLooping: " << (int)audioClipPtr->isLooping());
            DBG("    loopStartBeats: " << audioClipPtr->getLoopStartBeats().inBeats());
            DBG("    loopLengthBeats: " << audioClipPtr->getLoopLengthBeats().inBeats());
            DBG("    loopStart: " << audioClipPtr->getLoopStart().inSeconds());
            DBG("    loopLength: " << audioClipPtr->getLoopLength().inSeconds());
            DBG("    offset: " << audioClipPtr->getPosition().getOffset().inSeconds());
        }

        double teSpeedRatio = clip->speedRatio;
        double currentSpeedRatio = audioClipPtr->getSpeedRatio();

        // Sync time stretch mode — warp also requires a valid stretcher
        auto desiredMode = static_cast<te::TimeStretcher::Mode>(clip->timeStretchMode);
        if (desiredMode == te::TimeStretcher::disabled &&
            (std::abs(teSpeedRatio - 1.0) > 0.001 || clip->warpEnabled))
            desiredMode = te::TimeStretcher::defaultMode;
        if (audioClipPtr->getTimeStretchMode() != desiredMode) {
            audioClipPtr->setTimeStretchMode(desiredMode);
        }

        if (std::abs(currentSpeedRatio - teSpeedRatio) > 0.001) {
            DBG("syncAudioClip [" << clipId << "] setSpeedRatio: " << teSpeedRatio << " (was "
                                  << currentSpeedRatio << ", speedRatio=" << clip->speedRatio
                                  << ")");
            audioClipPtr->setUsesProxy(false);
            audioClipPtr->setSpeedRatio(teSpeedRatio);

            // Log TE state after setSpeedRatio (which internally calls setLoopRange)
            auto posAfterSpeed = audioClipPtr->getPosition();
            auto loopRangeAfterSpeed = audioClipPtr->getLoopRange();
            DBG("  TE after setSpeedRatio: offset="
                << posAfterSpeed.getOffset().inSeconds()
                << ", start=" << posAfterSpeed.getStart().inSeconds()
                << ", end=" << posAfterSpeed.getEnd().inSeconds()
                << ", loopRange=" << loopRangeAfterSpeed.getStart().inSeconds() << "-"
                << loopRangeAfterSpeed.getEnd().inSeconds()
                << ", autoTempo=" << (int)audioClipPtr->getAutoTempo()
                << ", isLooping=" << (int)audioClipPtr->isLooping());
        }

        // Sync warp state to engine
        if (clip->warpEnabled != audioClipPtr->getWarpTime()) {
            audioClipPtr->setWarpTime(clip->warpEnabled);
        }
    }

    // 6. UPDATE loop properties (BEFORE offset — setLoopRangeBeats can reset offset)
    // Use beat-based loop range in auto-tempo/warp mode, time-based otherwise
    if (clip->autoTempo || clip->warpEnabled) {
        // Auto-tempo mode: ALWAYS set beat-based loop range
        // The loop range defines the clip's musical extent (not just the loop region)

        // Get tempo for beat calculations
        double bpm = edit_.tempoSequence.getTempo(0)->getBpm();
        DBG("  Current BPM: " << bpm);

        // Override TE's loopInfo BPM to match our calibrated sourceBPM.
        // setAutoTempo calibrates sourceBPM = projectBPM / speedRatio so that
        // enabling autoTempo doesn't change playback speed.  TE uses loopInfo
        // to map source beats ↔ source time, so the two must agree.
        if (clip->sourceBPM > 0.0) {
            auto waveInfo = audioClipPtr->getWaveInfo();
            auto& li = audioClipPtr->getLoopInfo();
            double currentLoopInfoBpm = li.getBpm(waveInfo);
            if (std::abs(currentLoopInfoBpm - clip->sourceBPM) > 0.1) {
                DBG("  -> Overriding TE loopInfo BPM: " << currentLoopInfoBpm << " -> "
                                                        << clip->sourceBPM);
                li.setBpm(clip->sourceBPM, waveInfo);
            }
        }

        // Calculate beat range using centralized helper
        auto [loopStartBeats, loopLengthBeats] = ClipOperations::getAutoTempoBeatRange(*clip, bpm);

        DBG("  -> Beat range (from ClipOperations): start="
            << loopStartBeats << ", length=" << loopLengthBeats << " beats"
            << ", end=" << (loopStartBeats + loopLengthBeats));
        DBG("  -> TE loopInfo.getNumBeats(): " << audioClipPtr->getLoopInfo().getNumBeats());

        // Set the beat-based loop range in TE
        auto loopRange = te::BeatRange(te::BeatPosition::fromBeats(loopStartBeats),
                                       te::BeatDuration::fromBeats(loopLengthBeats));

        DBG("  -> Calling audioClipPtr->setLoopRangeBeats()");
        audioClipPtr->setLoopRangeBeats(loopRange);
        DBG("  TE STATE AFTER setLoopRangeBeats:");
        DBG("    isLooping: " << (int)audioClipPtr->isLooping());
        DBG("    loopStartBeats: " << audioClipPtr->getLoopStartBeats().inBeats());
        DBG("    loopLengthBeats: " << audioClipPtr->getLoopLengthBeats().inBeats());
        DBG("    loopStart: " << audioClipPtr->getLoopStart().inSeconds());
        DBG("    loopLength: " << audioClipPtr->getLoopLength().inSeconds());
        DBG("    offset: " << audioClipPtr->getPosition().getOffset().inSeconds());
        DBG("    autoTempo: " << (int)audioClipPtr->getAutoTempo());
        DBG("    speedRatio: " << audioClipPtr->getSpeedRatio());

        if (!audioClipPtr->isLooping()) {
            DBG("  -> WARNING: TE isLooping() is FALSE after setLoopRangeBeats!");
        }
    } else {
        // Time-based mode: Use time-based loop range
        // Only use setLoopRange (time-based), NOT setLoopRangeBeats which forces
        // autoTempo=true and speedRatio=1.0, breaking time-stretch.
        if (clip->loopEnabled && clip->getSourceLength() > 0.0) {
            auto loopStartTime = te::TimePosition::fromSeconds(clip->getTeLoopStart());
            auto loopEndTime = te::TimePosition::fromSeconds(clip->getTeLoopEnd());
            audioClipPtr->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
        } else if (audioClipPtr->isLooping()) {
            // Looping disabled in our model but TE still has it on — clear it
            DBG("syncAudioClip [" << clipId << "] clearing TE loop range (our loopEnabled=false)");
            audioClipPtr->setLoopRange({});
        }
    }

    // 7. UPDATE audio offset (trim point in file)
    // Must come AFTER loop range — setLoopRangeBeats resets offset internally
    {
        double teOffset = juce::jmax(0.0, clip->getTeOffset(clip->loopEnabled));
        auto currentOffset = audioClipPtr->getPosition().getOffset().inSeconds();
        DBG("  OFFSET SYNC: teOffset=" << teOffset << " (offset=" << clip->offset << " loopStart="
                                       << clip->loopStart << " speedRatio=" << clip->speedRatio
                                       << " loopEnabled=" << (int)clip->loopEnabled << ")"
                                       << ", currentTEOffset=" << currentOffset);
        if (std::abs(currentOffset - teOffset) > 0.001) {
            audioClipPtr->setOffset(te::TimeDuration::fromSeconds(teOffset));
            DBG("    -> setOffset(" << teOffset << ")");
        }
    }

    // 8. PITCH
    if (clip->autoPitch != audioClipPtr->getAutoPitch())
        audioClipPtr->setAutoPitch(clip->autoPitch);
    if (static_cast<int>(audioClipPtr->getAutoPitchMode()) != clip->autoPitchMode)
        audioClipPtr->setAutoPitchMode(
            static_cast<te::AudioClipBase::AutoPitchMode>(clip->autoPitchMode));
    if (std::abs(audioClipPtr->getPitchChange() - clip->pitchChange) > 0.001f)
        audioClipPtr->setPitchChange(clip->pitchChange);
    if (audioClipPtr->getTransposeSemiTones(false) != clip->transpose)
        audioClipPtr->setTranspose(clip->transpose);

    // 9. BEAT DETECTION
    if (clip->autoDetectBeats != audioClipPtr->getAutoDetectBeats())
        audioClipPtr->setAutoDetectBeats(clip->autoDetectBeats);
    if (std::abs(audioClipPtr->getBeatSensitivity() - clip->beatSensitivity) > 0.001f)
        audioClipPtr->setBeatSensitivity(clip->beatSensitivity);

    // 10. PLAYBACK (isReversed handled at top of function)

    // 11. PER-CLIP MIX
    if (std::abs(audioClipPtr->getGainDB() - clip->gainDB) > 0.001f)
        audioClipPtr->setGainDB(clip->gainDB);
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

    // Final state dump
    {
        auto finalPos = audioClipPtr->getPosition();
        auto finalLoop = audioClipPtr->getLoopRange();
        auto finalLoopBeats = audioClipPtr->getLoopRangeBeats();

        DBG("========== FINAL STATE [" << clipId << "] ==========");
        DBG("  TE Position: " << finalPos.getStart().inSeconds() << " - "
                              << finalPos.getEnd().inSeconds());
        DBG("  TE Offset: " << finalPos.getOffset().inSeconds());
        DBG("  TE SpeedRatio: " << audioClipPtr->getSpeedRatio());
        DBG("  TE AutoTempo: " << (int)audioClipPtr->getAutoTempo());
        DBG("  TE IsLooping: " << (int)audioClipPtr->isLooping());
        DBG("  TE LoopRange (time): " << finalLoop.getStart().inSeconds() << " - "
                                      << finalLoop.getEnd().inSeconds());
        DBG("  TE LoopRangeBeats: "
            << finalLoopBeats.getStart().inBeats() << " - "
            << (finalLoopBeats.getStart() + finalLoopBeats.getLength()).inBeats()
            << " (length: " << finalLoopBeats.getLength().inBeats() << " beats)");
        DBG("  Our offset: " << clip->offset);
        DBG("  Our speedRatio: " << clip->speedRatio);
        DBG("  Our loopEnabled: " << (int)clip->loopEnabled);
        DBG("  Our autoTempo: " << (int)clip->autoTempo);
        DBG("=============================================");
    }
}

}  // namespace magda
