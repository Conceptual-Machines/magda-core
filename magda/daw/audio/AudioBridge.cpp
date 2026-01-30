#include "AudioBridge.hpp"

#include <iostream>
#include <unordered_set>

#include "../engine/PluginWindowManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"

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

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit) : engine_(engine), edit_(edit) {
    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Master metering will be registered when playback context is available
    // (done in timerCallback when context exists)

    // Start timer for metering updates (30 FPS for smooth UI)
    startTimerHz(30);

    std::cout << "AudioBridge initialized" << std::endl;
}

AudioBridge::~AudioBridge() {
    std::cout << "AudioBridge::~AudioBridge - starting cleanup" << std::endl;

    // CRITICAL: Acquire lock BEFORE stopping timer to ensure proper synchronization.
    // This prevents race condition where timerCallback() could be running while
    // we're destroying member variables. By holding the lock across stopTimer(),
    // we guarantee that any running timer callback completes before destruction.
    {
        juce::ScopedLock lock(mappingLock_);

        // Set shutdown flag while holding lock to prevent new timer operations
        isShuttingDown_.store(true, std::memory_order_release);

        // Stop timer while holding lock - ensures no callback is running when we proceed
        stopTimer();

        // Now safe to remove listeners as timer is stopped and shutdown flag is set
        TrackManager::getInstance().removeListener(this);
        ClipManager::getInstance().removeListener(this);

        // NOTE: Plugin windows are now closed by PluginWindowManager BEFORE AudioBridge
        // is destroyed (in TracktionEngineWrapper::shutdown()). No window cleanup needed here.

        // Unregister master meter client from playback context
        if (masterMeterRegistered_) {
            if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                ctx->masterLevels.removeClient(masterMeterClient_);
            }
        }

        // Unregister all track meter clients
        for (auto& [trackId, track] : trackMapping_) {
            if (track) {
                auto* levelMeter = track->getLevelMeterPlugin();
                if (levelMeter) {
                    auto it = meterClients_.find(trackId);
                    if (it != meterClients_.end()) {
                        levelMeter->measurer.removeClient(it->second);
                    }
                }
            }
        }

        // Clear all mappings - safe now as timer is stopped and lock is held
        trackMapping_.clear();
        deviceToPlugin_.clear();
        pluginToDevice_.clear();
        meterClients_.clear();
    }

    std::cout << "AudioBridge destroyed" << std::endl;
}

// =============================================================================
// TrackManagerListener implementation
// =============================================================================

void AudioBridge::tracksChanged() {
    // Tracks were added/removed/reordered - sync all
    syncAll();
}

void AudioBridge::trackPropertyChanged(int trackId) {
    // Track property changed (volume, pan, mute, solo) - sync to Tracktion Engine
    auto* track = getAudioTrack(trackId);
    if (track) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (trackInfo) {
            // Sync mute/solo to track
            track->setMute(trackInfo->muted);
            track->setSolo(trackInfo->soloed);

            // Sync volume/pan to VolumeAndPanPlugin
            setTrackVolume(trackId, trackInfo->volume);
            setTrackPan(trackId, trackInfo->pan);
        }
    }
}

void AudioBridge::trackDevicesChanged(TrackId trackId) {
    // Devices on a track changed - resync that track's plugins
    syncTrackPlugins(trackId);
}

void AudioBridge::masterChannelChanged() {
    // Master channel property changed - sync to Tracktion Engine
    const auto& master = TrackManager::getInstance().getMasterChannel();
    setMasterVolume(master.volume);
    setMasterPan(master.pan);

    // TODO: Handle master mute (may need different approach than track mute)
}

void AudioBridge::deviceParameterChanged(DeviceId deviceId, int paramIndex, float newValue) {
    // A single device parameter changed - sync only that parameter to processor
    auto* processor = getDeviceProcessor(deviceId);
    if (!processor) {
        return;
    }

    // For ExternalPluginProcessor, use setParameterByIndex for efficient single-param sync
    if (auto* extProcessor = dynamic_cast<ExternalPluginProcessor*>(processor)) {
        extProcessor->setParameterByIndex(paramIndex, newValue);
    }
}

void AudioBridge::devicePropertyChanged(DeviceId deviceId) {
    // A device property changed (gain, bypass, etc.) - sync to processor
    DBG("AudioBridge::devicePropertyChanged deviceId=" << deviceId);

    auto* processor = getDeviceProcessor(deviceId);
    if (!processor) {
        DBG("  No processor found for deviceId=" << deviceId);
        return;
    }

    // Find the DeviceInfo to get updated values
    // We need to search through all tracks to find this device
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        for (const auto& element : track.chainElements) {
            if (std::holds_alternative<DeviceInfo>(element)) {
                const auto& device = std::get<DeviceInfo>(element);
                if (device.id == deviceId) {
                    DBG("  Found device in track " << track.id << ", syncing...");
                    // Sync processor from the updated DeviceInfo
                    processor->syncFromDeviceInfo(device);
                    return;
                }
            }
        }
    }
    DBG("  Device not found in any track!");
}

// =============================================================================
// ClipManagerListener implementation
// =============================================================================

void AudioBridge::clipsChanged() {
    // If we're shutting down, don't attempt to modify the engine graph
    if (isShuttingDown_.load(std::memory_order_acquire))
        return;

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
    for (const auto& [clipId, engineId] : clipIdToEngineId_) {
        if (currentClipIds.find(clipId) == currentClipIds.end()) {
            clipsToRemove.push_back(clipId);
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

void AudioBridge::clipPropertyChanged(ClipId clipId) {
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip) {
        DBG("AudioBridge::clipPropertyChanged: clip " << clipId << " not found in ClipManager");
        return;
    }

    if (clip->view == ClipView::Session) {
        DBG("AudioBridge::clipPropertyChanged: SESSION clip "
            << clipId << " | loopEnabled=" << (int)clip->internalLoopEnabled
            << " loopLength=" << clip->internalLoopLength << " length=" << clip->length
            << " launchQ=" << (int)clip->launchQuantize << " sceneIndex=" << clip->sceneIndex);

        // Session clip property changed (e.g. sceneIndex set after creation).
        // Try to sync it to a slot if not already synced.
        if (clip->sceneIndex >= 0) {
            bool synced = syncSessionClipToSlot(clipId);
            DBG("  syncSessionClipToSlot returned "
                << (synced ? "true (new sync)" : "false (already synced or error)"));

            if (synced) {
                // New clip synced — rebuild graph so SlotControlNode is created
                if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                    ctx->reallocate();
                }
            } else {
                // Clip already synced — propagate property changes to TE clip
                auto* teClip = getSessionTeClip(clipId);
                if (teClip) {
                    // Update launch quantization
                    auto* lq = teClip->getLaunchQuantisation();
                    if (lq) {
                        lq->type = toTELaunchQType(clip->launchQuantize);
                    }

                    // Update clip's own loop state
                    if (clip->internalLoopEnabled) {
                        auto& tempoSeq = edit_.tempoSequence;
                        auto loopEndBeat = te::BeatPosition::fromBeats(clip->internalLoopLength);
                        auto loopEndTime = tempoSeq.beatsToTime(loopEndBeat);

                        teClip->setLoopRange(
                            te::TimeRange(te::TimePosition::fromSeconds(0.0), loopEndTime));
                        teClip->setLoopRangeBeats({te::BeatPosition::fromBeats(0.0), loopEndBeat});
                    } else {
                        teClip->disableLooping();
                    }

                    // Update looping on the launch handle
                    auto launchHandle = teClip->getLaunchHandle();
                    if (launchHandle) {
                        if (clip->internalLoopEnabled) {
                            launchHandle->setLooping(
                                te::BeatDuration::fromBeats(clip->internalLoopLength));
                        } else {
                            launchHandle->setLooping(std::nullopt);
                        }
                    }

                    // Re-sync MIDI notes from ClipManager to the TE MidiClip
                    if (clip->type == ClipType::MIDI) {
                        if (auto* midiClip = dynamic_cast<te::MidiClip*>(teClip)) {
                            auto& sequence = midiClip->getSequence();
                            sequence.clear(nullptr);
                            for (const auto& note : clip->midiNotes) {
                                sequence.addNote(note.noteNumber,
                                                 te::BeatPosition::fromBeats(note.startBeat),
                                                 te::BeatDuration::fromBeats(note.lengthBeats),
                                                 note.velocity, 0, nullptr);
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

void AudioBridge::clipSelectionChanged(ClipId clipId) {
    // Selection changed - we don't need to do anything here
    // The UI will handle this
    juce::ignoreUnused(clipId);
}

// =============================================================================
// Clip Synchronization
// =============================================================================

void AudioBridge::syncClipToEngine(ClipId clipId) {
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

void AudioBridge::syncMidiClipToEngine(ClipId clipId, const ClipInfo* clip) {
    // Get the Tracktion AudioTrack for this MAGDA track
    auto* audioTrack = getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("syncClipToEngine: Tracktion track not found for MAGDA track: " << clip->trackId);
        return;
    }

    DBG("syncClipToEngine: Found audio track for MAGDA track " << clip->trackId);

    namespace te = tracktion;
    te::MidiClip* midiClipPtr = nullptr;

    // Check if clip already exists in Tracktion Engine
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
            DBG("syncClipToEngine: Clip " << clipId
                                          << " mapping exists but clip not found in engine");
            // Clear stale mapping and recreate
            clipIdToEngineId_.erase(it);
            engineIdToClipId_.erase(engineId);
        } else {
            DBG("syncClipToEngine: Updating existing clip " << clipId);
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
        clipIdToEngineId_[clipId] = engineClipId;
        engineIdToClipId_[engineClipId] = clipId;

        DBG("syncClipToEngine: Created new clip " << clipId);
    }

    // Update clip position/length
    // CRITICAL: Use preserveSync=true to maintain the content offset
    // When false, Tracktion adjusts the content offset which breaks note playback
    midiClipPtr->setStart(te::TimePosition::fromSeconds(clip->startTime), true, false);
    midiClipPtr->setEnd(te::TimePosition::fromSeconds(clip->startTime + clip->length), false);

    // Force offset to 0 to ensure notes play from clip start
    midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

    // Clear existing notes and add all notes from ClipManager
    auto& sequence = midiClipPtr->getSequence();
    sequence.clear(nullptr);  // Clear all notes

    // Get tempo info for conversion verification
    auto& tempoSeq = midiClipPtr->edit.tempoSequence;
    auto clipStartTime = te::TimePosition::fromSeconds(clip->startTime);
    auto clipStartBeat = tempoSeq.timeToBeats(clipStartTime);

    DBG("=== syncClipToEngine Debug ===");
    DBG("Clip ID: " << clipId);
    DBG("Clip startTime: " << clip->startTime << "s, length: " << clip->length << "s");
    DBG("Clip END time: " << (clip->startTime + clip->length) << "s");
    DBG("Clip startBeat on timeline: " << clipStartBeat.inBeats());
    DBG("Current tempo: " << tempoSeq.getTempoAt(clipStartTime).getBpm());
    DBG("Number of notes: " << clip->midiNotes.size());

    // Calculate clip end beat for boundary checking
    auto clipEndTime = te::TimePosition::fromSeconds(clip->startTime + clip->length);
    auto clipEndBeat = tempoSeq.timeToBeats(clipEndTime);
    double clipLengthInBeats = clipEndBeat.inBeats() - clipStartBeat.inBeats();
    DBG("Clip length in beats: " << clipLengthInBeats);

    for (const auto& note : clip->midiNotes) {
        te::BeatPosition startBeat = te::BeatPosition::fromBeats(note.startBeat);
        te::BeatDuration lengthBeats = te::BeatDuration::fromBeats(note.lengthBeats);

        // Convert to absolute timeline time for verification
        auto noteAbsoluteTime = tempoSeq.beatsToTime(
            te::BeatPosition::fromBeats(clipStartBeat.inBeats() + note.startBeat));
        auto noteEndTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(
            clipStartBeat.inBeats() + note.startBeat + note.lengthBeats));

        // Check if note is within clip boundary
        bool noteStartWithinClip = note.startBeat < clipLengthInBeats;
        bool noteEndWithinClip = (note.startBeat + note.lengthBeats) <= clipLengthInBeats;

        DBG("  Note " << note.noteNumber << ": clip-relative beat=" << note.startBeat
                      << ", length=" << note.lengthBeats << ", velocity=" << note.velocity);
        DBG("    -> Absolute timeline: " << noteAbsoluteTime.inSeconds() << "s to "
                                         << noteEndTime.inSeconds() << "s");
        DBG("    -> Within clip boundary? start=" << (noteStartWithinClip ? "YES" : "NO")
                                                  << ", end="
                                                  << (noteEndWithinClip ? "YES" : "NO"));

        sequence.addNote(note.noteNumber, startBeat, lengthBeats, note.velocity,
                         0,         // colour index
                         nullptr);  // undo manager
    }

    DBG("syncClipToEngine: Synced clip " << clipId << " with " << clip->midiNotes.size()
                                         << " notes");
}

void AudioBridge::syncAudioClipToEngine(ClipId clipId, const ClipInfo* clip) {
    namespace te = tracktion;

    // 1. Get Tracktion track
    auto* audioTrack = getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("AudioBridge: Track not found for audio clip " << clipId);
        return;
    }

    // 2. Check if clip already synced
    te::WaveAudioClip* audioClipPtr = nullptr;
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
            DBG("AudioBridge: Clip mapping stale, recreating for clip " << clipId);
            clipIdToEngineId_.erase(it);
            engineIdToClipId_.erase(engineId);
        }
    }

    // 3. CREATE new clip if doesn't exist
    if (!audioClipPtr) {
        if (clip->audioSources.empty()) {
            DBG("AudioBridge: No audio sources for clip " << clipId);
            return;
        }
        const auto& source = clip->audioSources[0];
        juce::File audioFile(source.filePath);
        if (!audioFile.existsAsFile()) {
            DBG("AudioBridge: Audio file not found: " << source.filePath);
            return;
        }

        double createStart = clip->startTime + source.position;
        double createEnd = createStart + source.length;
        auto timeRange = te::TimeRange(te::TimePosition::fromSeconds(createStart),
                                       te::TimePosition::fromSeconds(createEnd));

        auto clipRef =
            insertWaveClip(*audioTrack, audioFile.getFileNameWithoutExtension(), audioFile,
                           te::ClipPosition{timeRange}, te::DeleteExistingClips::no);

        if (!clipRef) {
            DBG("AudioBridge: Failed to create WaveAudioClip");
            return;
        }

        audioClipPtr = clipRef.get();

        // Enable timestretcher at creation time (before any speedRatio changes)
        // Must be set before setSpeedRatio() to avoid assertion failures
        audioClipPtr->setTimeStretchMode(te::TimeStretcher::defaultMode);

        // Store bidirectional mapping
        std::string engineClipId = audioClipPtr->itemID.toString().toStdString();
        clipIdToEngineId_[clipId] = engineClipId;
        engineIdToClipId_[engineClipId] = clipId;

        DBG("AudioBridge: Created WaveAudioClip (engine ID: " << engineClipId << ")");
    }

    // 4. UPDATE clip position/length using audio source position within clip
    // The engine clip plays audio starting at clip->startTime + source.position,
    // for source.length duration, reading from source.offset in the file.
    // IMPORTANT: Clamp to clip boundaries so audio stops at visual clip end.
    double clipStart = clip->startTime;
    double clipEnd = clip->startTime + clip->length;
    double engineStart = clipStart;
    double engineEnd = clipEnd;
    double engineOffset = 0.0;

    if (!clip->audioSources.empty()) {
        const auto& source = clip->audioSources[0];
        double sourceStart = clip->startTime + source.position;
        double sourceEnd = sourceStart + source.length;

        // Clamp engine boundaries to clip boundaries
        // Audio should only play within the visible clip region
        engineStart = std::max(sourceStart, clipStart);
        engineEnd = std::min(sourceEnd, clipEnd);

        // Adjust offset if source starts before clip (left side clipped)
        if (sourceStart < clipStart) {
            double clippedTime = clipStart - sourceStart;
            engineOffset = source.offset + (clippedTime / source.stretchFactor);
        } else {
            engineOffset = source.offset;
        }
    }

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

    // 5. UPDATE audio offset (trim point in file)
    auto currentOffset = audioClipPtr->getPosition().getOffset().inSeconds();
    if (std::abs(currentOffset - engineOffset) > 0.001) {
        audioClipPtr->setOffset(te::TimeDuration::fromSeconds(engineOffset));
    }

    // 6. UPDATE speed ratio for time-stretching
    // TE speedRatio: 1.0 = normal, 2.0 = 2x faster, 0.5 = 2x slower
    // Our stretchFactor: 1.0 = normal, 2.0 = 2x slower, 0.5 = 2x faster
    // Mapping: TE speedRatio = 1.0 / stretchFactor
    if (!clip->audioSources.empty()) {
        const auto& source = clip->audioSources[0];
        double teSpeedRatio = 1.0 / source.stretchFactor;
        double currentSpeedRatio = audioClipPtr->getSpeedRatio();

        if (std::abs(currentSpeedRatio - teSpeedRatio) > 0.001) {
            // Ensure timestretcher is enabled (may not be set for pre-existing clips)
            if (audioClipPtr->getTimeStretchMode() == te::TimeStretcher::disabled) {
                audioClipPtr->setTimeStretchMode(te::TimeStretcher::defaultMode);
            }
            // Disable autoTempo which blocks setSpeedRatio in AudioClipBase
            if (audioClipPtr->getAutoTempo()) {
                audioClipPtr->setAutoTempo(false);
            }
            audioClipPtr->setSpeedRatio(teSpeedRatio);
        }
    }
}

void AudioBridge::removeClipFromEngine(ClipId clipId) {
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

// =============================================================================
// Session Clip Lifecycle (slot-based)
// =============================================================================

bool AudioBridge::syncSessionClipToSlot(ClipId clipId) {
    namespace te = tracktion;

    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip) {
        DBG("AudioBridge::syncSessionClipToSlot: Clip " << clipId << " not found in ClipManager");
        return false;
    }
    if (clip->view != ClipView::Session || clip->sceneIndex < 0)
        return false;

    auto* audioTrack = getAudioTrack(clip->trackId);
    if (!audioTrack) {
        DBG("AudioBridge::syncSessionClipToSlot: Track " << clip->trackId << " not found for clip "
                                                         << clipId);
        return false;
    }

    // Ensure enough scenes (and slots on all tracks) exist
    edit_.getSceneList().ensureNumberOfScenes(clip->sceneIndex + 1);

    // Get the slot for this clip
    auto slots = audioTrack->getClipSlotList().getClipSlots();

    if (clip->sceneIndex >= static_cast<int>(slots.size())) {
        DBG("AudioBridge::syncSessionClipToSlot: Slot index out of range for clip " << clipId);
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
        if (clip->audioSources.empty())
            return false;

        const auto& source = clip->audioSources[0];
        juce::File audioFile(source.filePath);
        if (!audioFile.existsAsFile()) {
            DBG("AudioBridge::syncSessionClipToSlot: Audio file not found: " << source.filePath);
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

        // Enable timestretcher
        audioClipPtr->setTimeStretchMode(te::TimeStretcher::defaultMode);

        // Set file offset (trim point)
        audioClipPtr->setOffset(te::TimeDuration::fromSeconds(source.offset));

        // Set speed ratio from stretch factor
        if (std::abs(source.stretchFactor - 1.0) > 0.001) {
            double teSpeedRatio = 1.0 / source.stretchFactor;
            if (audioClipPtr->getAutoTempo()) {
                audioClipPtr->setAutoTempo(false);
            }
            audioClipPtr->setSpeedRatio(teSpeedRatio);
        }

        // Set looping properties
        if (clip->internalLoopEnabled) {
            auto& tempoSeq = edit_.tempoSequence;
            auto loopEndBeat = te::BeatPosition::fromBeats(clip->internalLoopLength);
            auto loopEndTime = tempoSeq.beatsToTime(loopEndBeat);
            double loopLengthSeconds = loopEndTime.inSeconds();

            audioClipPtr->setLoopRange(
                te::TimeRange(te::TimePosition::fromSeconds(0.0),
                              te::TimePosition::fromSeconds(loopLengthSeconds)));
            audioClipPtr->setLoopRangeBeats(
                {te::BeatPosition::fromBeats(0.0),
                 te::BeatPosition::fromBeats(clip->internalLoopLength)});
        }

        // Set per-clip launch quantization
        audioClipPtr->setUsesGlobalLaunchQuatisation(false);
        if (auto* lq = audioClipPtr->getLaunchQuantisation()) {
            lq->type = toTELaunchQType(clip->launchQuantize);
        }

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

        // Force offset to 0
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

        // Add MIDI notes
        auto& sequence = midiClipPtr->getSequence();
        for (const auto& note : clip->midiNotes) {
            te::BeatPosition noteBeat = te::BeatPosition::fromBeats(note.startBeat);
            te::BeatDuration noteLength = te::BeatDuration::fromBeats(note.lengthBeats);
            sequence.addNote(note.noteNumber, noteBeat, noteLength, note.velocity, 0, nullptr);
        }

        // Set looping if enabled
        if (clip->internalLoopEnabled) {
            midiClipPtr->setLoopRangeBeats({te::BeatPosition::fromBeats(0.0),
                                            te::BeatPosition::fromBeats(clip->internalLoopLength)});
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

void AudioBridge::removeSessionClipFromSlot(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (teClip)
        teClip->removeFromParent();
}

void AudioBridge::launchSessionClip(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip) {
        DBG("AudioBridge::launchSessionClip: TE clip not found for clip " << clipId);
        return;
    }

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle) {
        DBG("AudioBridge::launchSessionClip: No LaunchHandle for clip " << clipId);
        return;
    }

    // Set looping before play
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip) {
        if (clip->internalLoopEnabled) {
            // Set clip's own loop range
            auto& tempoSeq = edit_.tempoSequence;
            auto loopEndBeat = te::BeatPosition::fromBeats(clip->internalLoopLength);
            auto loopEndTime = tempoSeq.beatsToTime(loopEndBeat);
            teClip->setLoopRange(te::TimeRange(te::TimePosition::fromSeconds(0.0), loopEndTime));
            teClip->setLoopRangeBeats({te::BeatPosition::fromBeats(0.0), loopEndBeat});

            // Set launch handle looping
            launchHandle->setLooping(te::BeatDuration::fromBeats(clip->internalLoopLength));
        } else {
            teClip->disableLooping();
            launchHandle->setLooping(std::nullopt);
        }
    }

    launchHandle->play(std::nullopt);
}

void AudioBridge::stopSessionClip(ClipId clipId) {
    auto* teClip = getSessionTeClip(clipId);
    if (!teClip)
        return;

    auto launchHandle = teClip->getLaunchHandle();
    if (!launchHandle)
        return;

    launchHandle->stop(std::nullopt);

    // Send all-notes-off (CC 123) on all channels for MIDI clips to prevent stuck notes
    const auto* clip = ClipManager::getInstance().getClip(clipId);
    if (clip && clip->type == ClipType::MIDI) {
        auto& dm = engine_.getDeviceManager();
        for (int i = 0; i < dm.getNumMidiOutDevices(); ++i) {
            if (auto* midiOut = dm.getMidiOutDevice(i)) {
                if (midiOut->isEnabled()) {
                    for (int ch = 1; ch <= 16; ++ch) {
                        midiOut->fireMessage(juce::MidiMessage::allNotesOff(ch));
                    }
                }
            }
        }
    }
}

te::Clip* AudioBridge::getSessionTeClip(ClipId clipId) {
    auto& cm = ClipManager::getInstance();
    const auto* clip = cm.getClip(clipId);
    if (!clip || clip->view != ClipView::Session || clip->sceneIndex < 0) {
        return nullptr;
    }

    auto* audioTrack = getAudioTrack(clip->trackId);
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
// Plugin Loading
// =============================================================================

te::Plugin::Ptr AudioBridge::loadBuiltInPlugin(const TrackId TRACK_ID, const juce::String& type) {
    auto* track = getAudioTrack(TRACK_ID);
    if (!track) {
        // Create track if it doesn't exist
        auto* trackInfo = TrackManager::getInstance().getTrack(TRACK_ID);
        juce::String name = trackInfo ? trackInfo->name : "Track";
        track = createAudioTrack(TRACK_ID, name);
    }

    if (!track)
        return nullptr;

    te::Plugin::Ptr plugin;

    if (type.equalsIgnoreCase("tone") || type.equalsIgnoreCase("tonegenerator")) {
        plugin = createToneGenerator(track);
        // Note: "volume" is NOT a device type - track volume is separate infrastructure
        // managed by ensureVolumePluginPosition() and controlled via TrackManager
    } else if (type.equalsIgnoreCase("meter") || type.equalsIgnoreCase("levelmeter")) {
        plugin = createLevelMeter(track);
    } else if (type.equalsIgnoreCase("delay")) {
        plugin = edit_.getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, {});
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase("reverb")) {
        plugin = edit_.getPluginCache().createNewPlugin(te::ReverbPlugin::xmlTypeName, {});
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase("eq") || type.equalsIgnoreCase("equaliser")) {
        plugin = edit_.getPluginCache().createNewPlugin(te::EqualiserPlugin::xmlTypeName, {});
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase("compressor")) {
        plugin = edit_.getPluginCache().createNewPlugin(te::CompressorPlugin::xmlTypeName, {});
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase("chorus")) {
        plugin = edit_.getPluginCache().createNewPlugin(te::ChorusPlugin::xmlTypeName, {});
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase("phaser")) {
        plugin = edit_.getPluginCache().createNewPlugin(te::PhaserPlugin::xmlTypeName, {});
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    }

    if (!plugin) {
        std::cerr << "Failed to load built-in plugin: " << type << std::endl;
    }

    return plugin;
}

PluginLoadResult AudioBridge::loadExternalPlugin(TrackId trackId,
                                                 const juce::PluginDescription& description) {
    MAGDA_MONITOR_SCOPE("PluginLoad");

    auto* track = getAudioTrack(trackId);
    if (!track) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        juce::String name = trackInfo ? trackInfo->name : "Track";
        track = createAudioTrack(trackId, name);
    }

    if (!track) {
        return PluginLoadResult::Failure("Failed to create or find track for plugin");
    }

    try {
        // Debug: log the full description being used
        DBG("loadExternalPlugin: Creating plugin with description:");
        DBG("  name: " << description.name);
        DBG("  fileOrIdentifier: " << description.fileOrIdentifier);
        DBG("  uniqueId: " << description.uniqueId);
        DBG("  deprecatedUid: " << description.deprecatedUid);
        DBG("  isInstrument: " << (description.isInstrument ? "true" : "false"));
        DBG("  createIdentifierString: " << description.createIdentifierString());

        // WORKAROUND for Tracktion Engine bug: When multiple plugins share the same
        // uniqueId (common in VST3 bundles with multiple components like Serum 2 + Serum 2 FX),
        // TE's findMatchingPlugin() matches by uniqueId first and returns the wrong plugin.
        // By clearing uniqueId, we force it to fall through to deprecatedUid matching,
        // which correctly distinguishes between plugins in the same bundle.
        juce::PluginDescription descCopy = description;
        if (descCopy.deprecatedUid != 0) {
            DBG("  Clearing uniqueId to force deprecatedUid matching (workaround for TE bug)");
            descCopy.uniqueId = 0;
        }

        // Create external plugin using the description
        auto plugin =
            edit_.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, descCopy);

        if (plugin) {
            // Check if plugin actually initialized successfully
            if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                // Debug: Check what plugin was actually created
                DBG("ExternalPlugin created - checking actual plugin:");
                DBG("  Requested: " << description.name << " (uniqueId=" << description.uniqueId
                                    << ")");
                DBG("  Got: " << extPlugin->getName()
                              << " (identifier=" << extPlugin->getIdentifierString() << ")");

                // Check if the plugin file exists and is loadable
                if (!extPlugin->isEnabled()) {
                    juce::String error = "Plugin failed to initialize: " + description.name;
                    if (description.fileOrIdentifier.isNotEmpty()) {
                        error += " (" + description.fileOrIdentifier + ")";
                    }
                    return PluginLoadResult::Failure(error);
                }
            }

            track->pluginList.insertPlugin(plugin, -1, nullptr);
            std::cout << "Loaded external plugin: " << description.name << " on track " << trackId
                      << std::endl;
            return PluginLoadResult::Success(plugin);
        } else {
            juce::String error = "Failed to create plugin: " + description.name;
            std::cerr << error << std::endl;
            return PluginLoadResult::Failure(error);
        }
    } catch (const std::exception& e) {
        juce::String error = "Exception loading plugin " + description.name + ": " + e.what();
        std::cerr << error << std::endl;
        return PluginLoadResult::Failure(error);
    } catch (...) {
        juce::String error = "Unknown exception loading plugin: " + description.name;
        std::cerr << error << std::endl;
        return PluginLoadResult::Failure(error);
    }
}

te::Plugin::Ptr AudioBridge::addLevelMeterToTrack(TrackId trackId) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        std::cerr << "Cannot add LevelMeter: track " << trackId << " not found" << std::endl;
        return nullptr;
    }

    // Remove any existing LevelMeter plugins first to avoid duplicates
    auto& plugins = track->pluginList;
    for (int i = plugins.size() - 1; i >= 0; --i) {
        if (auto* levelMeter = dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
            // Unregister meter client from the old LevelMeter
            {
                juce::ScopedLock lock(mappingLock_);
                auto it = meterClients_.find(trackId);
                if (it != meterClients_.end()) {
                    levelMeter->measurer.removeClient(it->second);
                }
            }

            levelMeter->deleteFromParent();
        }
    }

    // Now add a fresh LevelMeter at the end
    auto plugin = loadBuiltInPlugin(trackId, "levelmeter");

    // Register meter client with the new LevelMeter
    if (plugin) {
        if (auto* levelMeter = dynamic_cast<te::LevelMeterPlugin*>(plugin.get())) {
            juce::ScopedLock lock(mappingLock_);

            // Create or get existing client
            auto [it, inserted] = meterClients_.try_emplace(trackId);
            levelMeter->measurer.addClient(it->second);
        }
    }

    return plugin;
}

void AudioBridge::ensureVolumePluginPosition(te::AudioTrack* track) const {
    if (!track)
        return;

    auto& plugins = track->pluginList;

    // Find any VolumeAndPanPlugin in the chain
    te::Plugin::Ptr volPanPlugin;
    int volPanIndex = -1;

    for (int i = 0; i < plugins.size(); ++i) {
        if (dynamic_cast<te::VolumeAndPanPlugin*>(plugins[i])) {
            volPanPlugin = plugins[i];
            volPanIndex = i;
            break;
        }
    }

    if (!volPanPlugin) {
        // No VolumeAndPan found - create one at the end
        auto newPlugin = edit_.getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::create());
        if (newPlugin) {
            plugins.insertPlugin(newPlugin, -1, nullptr);
        }
        return;
    }

    // If VolumeAndPan is not at the end, move it there
    // (It will end up second-to-last after LevelMeter is added)
    int lastIndex = plugins.size() - 1;
    if (volPanIndex < lastIndex) {
        // Move to end: remove from current position and re-insert at end
        // Keep a reference to prevent deletion
        volPanPlugin->removeFromParent();
        plugins.insertPlugin(volPanPlugin, -1, nullptr);
    }
}

// =============================================================================
// Track Mapping
// =============================================================================

te::AudioTrack* AudioBridge::getAudioTrack(TrackId trackId) const {
    juce::ScopedLock lock(mappingLock_);
    auto it = trackMapping_.find(trackId);
    return it != trackMapping_.end() ? it->second : nullptr;
}

te::Plugin::Ptr AudioBridge::getPlugin(DeviceId deviceId) const {
    juce::ScopedLock lock(mappingLock_);
    auto it = deviceToPlugin_.find(deviceId);
    return it != deviceToPlugin_.end() ? it->second : nullptr;
}

DeviceProcessor* AudioBridge::getDeviceProcessor(DeviceId deviceId) const {
    juce::ScopedLock lock(mappingLock_);
    auto it = deviceProcessors_.find(deviceId);
    return it != deviceProcessors_.end() ? it->second.get() : nullptr;
}

te::AudioTrack* AudioBridge::createAudioTrack(TrackId trackId, const juce::String& name) {
    // Check if track already exists
    {
        juce::ScopedLock lock(mappingLock_);
        auto it = trackMapping_.find(trackId);
        if (it != trackMapping_.end() && it->second != nullptr) {
            return it->second;
        }
    }

    // Insert new track at the end
    auto insertPoint = te::TrackInsertPoint(nullptr, nullptr);
    auto trackPtr = edit_.insertNewAudioTrack(insertPoint, nullptr);

    te::AudioTrack* track = trackPtr.get();
    if (track) {
        track->setName(name);

        // Route track output to master/default output
        track->getOutput().setOutputToDefaultDevice(false);  // false = audio (not MIDI)

        juce::ScopedLock lock(mappingLock_);
        trackMapping_[trackId] = track;

        // Don't register meter client yet - will do it when LevelMeter is added
        std::cout << "Created Tracktion AudioTrack for MAGDA track " << trackId << ": " << name
                  << " (routed to master)" << std::endl;
    }

    return track;
}

void AudioBridge::removeAudioTrack(TrackId trackId) {
    te::AudioTrack* track = nullptr;

    {
        juce::ScopedLock lock(mappingLock_);
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

    if (track) {
        edit_.deleteTrack(track);
        std::cout << "Removed Tracktion AudioTrack for MAGDA track " << trackId << std::endl;
    }
}

// =============================================================================
// Parameter Queue
// =============================================================================

bool AudioBridge::pushParameterChange(DeviceId deviceId, int paramIndex, float value) {
    ParameterChange change;
    change.deviceId = deviceId;
    change.paramIndex = paramIndex;
    change.value = value;
    change.source = ParameterChange::Source::User;
    return parameterQueue_.push(change);
}

// =============================================================================
// Synchronization
// =============================================================================

void AudioBridge::syncAll() {
    auto& tm = TrackManager::getInstance();
    const auto& tracks = tm.getTracks();

    for (const auto& track : tracks) {
        ensureTrackMapping(track.id);
        syncTrackPlugins(track.id);
    }

    // Sync master channel volume/pan to Tracktion Engine
    masterChannelChanged();
}

void AudioBridge::syncTrackPlugins(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    auto* teTrack = getAudioTrack(trackId);
    if (!teTrack) {
        teTrack = createAudioTrack(trackId, trackInfo->name);
    }

    if (!teTrack)
        return;

    // For Phase 1, we'll sync top-level devices on the track
    // (Full nested rack support comes in Phase 3)

    // Get current MAGDA devices
    std::vector<DeviceId> magdaDevices;
    for (const auto& element : trackInfo->chainElements) {
        if (std::holds_alternative<DeviceInfo>(element)) {
            magdaDevices.push_back(std::get<DeviceInfo>(element).id);
        }
    }

    // Remove TE plugins that no longer exist in MAGDA
    {
        juce::ScopedLock lock(mappingLock_);
        std::vector<DeviceId> toRemove;
        for (const auto& [deviceId, plugin] : deviceToPlugin_) {
            auto pluginIt = pluginToDevice_.find(plugin.get());
            if (pluginIt != pluginToDevice_.end()) {
                // Check if this plugin belongs to this track
                auto* owner = plugin->getOwnerTrack();
                if (owner == teTrack) {
                    // Check if device still exists in MAGDA
                    bool found = std::find(magdaDevices.begin(), magdaDevices.end(), deviceId) !=
                                 magdaDevices.end();
                    if (!found) {
                        toRemove.push_back(deviceId);
                    }
                }
            }
        }

        for (auto deviceId : toRemove) {
            // Close plugin window before removing device (via PluginWindowManager)
            if (windowManager_) {
                windowManager_->closeWindowsForDevice(deviceId);
            }

            auto it = deviceToPlugin_.find(deviceId);
            if (it != deviceToPlugin_.end()) {
                auto plugin = it->second;
                pluginToDevice_.erase(plugin.get());
                deviceToPlugin_.erase(it);
                plugin->deleteFromParent();
            }

            // Clean up device processor
            deviceProcessors_.erase(deviceId);
        }
    }

    // Add new plugins for MAGDA devices that don't have TE counterparts
    for (const auto& element : trackInfo->chainElements) {
        if (std::holds_alternative<DeviceInfo>(element)) {
            const auto& device = std::get<DeviceInfo>(element);

            juce::ScopedLock lock(mappingLock_);
            if (deviceToPlugin_.find(device.id) == deviceToPlugin_.end()) {
                // Load this device as a plugin
                auto plugin = loadDeviceAsPlugin(trackId, device);
                if (plugin) {
                    deviceToPlugin_[device.id] = plugin;
                    pluginToDevice_[plugin.get()] = device.id;
                }
            }
        }
    }

    // Ensure VolumeAndPan is near the end of the chain (before LevelMeter)
    // This is the track's fader control - it should come AFTER audio sources
    ensureVolumePluginPosition(teTrack);

    // Ensure LevelMeter is at the end of the plugin chain for metering
    addLevelMeterToTrack(trackId);
}

void AudioBridge::ensureTrackMapping(TrackId trackId) {
    if (!getAudioTrack(trackId)) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (trackInfo) {
            createAudioTrack(trackId, trackInfo->name);
        }
    }
}

// =============================================================================
// Audio Callback Support
// =============================================================================

void AudioBridge::processParameterChanges() {
    MAGDA_MONITOR_SCOPE("ParamChanges");

    ParameterChange change;
    while (parameterQueue_.pop(change)) {
        auto plugin = getPlugin(change.deviceId);
        if (plugin) {
            // NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign) - false positive from
            // profiling macros

            auto params = plugin->getAutomatableParameters();
            if (change.paramIndex >= 0 && change.paramIndex < static_cast<int>(params.size())) {
                params[static_cast<size_t>(change.paramIndex)]->setParameter(
                    change.value, juce::sendNotificationSync);
            }
        }
    }
}

// =============================================================================
// Transport State
// =============================================================================

void AudioBridge::updateTransportState(bool isPlaying, bool justStarted, bool justLooped) {
    // UI thread writes, audio thread reads - use release/acquire semantics
    transportPlaying_.store(isPlaying, std::memory_order_release);
    justStartedFlag_.store(justStarted, std::memory_order_release);
    justLoopedFlag_.store(justLooped, std::memory_order_release);

    // Enable/disable tone generators based on transport state
    juce::ScopedLock lock(mappingLock_);

    for (const auto& [deviceId, processor] : deviceProcessors_) {
        if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(processor.get())) {
            // Test Tone is always transport-synced
            // Simply bypass when stopped, enable when playing
            toneProc->setBypassed(!isPlaying);
            DBG("AudioBridge::updateTransportState - Tone generator device "
                << deviceId << " bypassed=" << (!isPlaying ? "YES" : "NO")
                << " (isPlaying=" << (isPlaying ? "YES" : "NO") << ")");
        }
    }
}

// =============================================================================
// MIDI Activity Monitoring
// =============================================================================

void AudioBridge::triggerMidiActivity(TrackId trackId) {
    if (trackId >= 0 && trackId < kMaxTracks) {
        midiActivityFlags_[trackId].store(true, std::memory_order_release);
    }
}

bool AudioBridge::consumeMidiActivity(TrackId trackId) {
    if (trackId >= 0 && trackId < kMaxTracks) {
        // Read and clear flag atomically
        return midiActivityFlags_[trackId].exchange(false, std::memory_order_acq_rel);
    }
    return false;
}

void AudioBridge::updateMetering() {
    // This would be called from the audio thread
    // For now, we use the timer callback for metering
}

void AudioBridge::onMidiDevicesAvailable() {
    // Called by TracktionEngineWrapper when MIDI devices become available
    DBG("AudioBridge::onMidiDevicesAvailable() - MIDI devices are now ready");

    // Log available MIDI devices
    auto& dm = engine_.getDeviceManager();
    auto midiDevices = dm.getMidiInDevices();
    DBG("  Available MIDI input devices: " << midiDevices.size());
    for (const auto& dev : midiDevices) {
        if (dev) {
            DBG("    - " << dev->getName() << " (enabled=" << (dev->isEnabled() ? "yes" : "no")
                         << ")");
        }
    }

    // Apply any pending MIDI routes
    applyPendingMidiRoutes();
}

void AudioBridge::applyPendingMidiRoutes() {
    if (pendingMidiRoutes_.empty()) {
        return;
    }

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext) {
        return;  // Still not ready
    }

    DBG("Applying " << pendingMidiRoutes_.size() << " pending MIDI routes");

    // Copy and clear to avoid re-entrancy issues
    auto routes = std::move(pendingMidiRoutes_);
    pendingMidiRoutes_.clear();

    for (const auto& [trackId, midiDeviceId] : routes) {
        setTrackMidiInput(trackId, midiDeviceId);
    }
}

void AudioBridge::timerCallback() {
    // Skip all operations if shutting down
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    // Apply any pending MIDI routes now that playback context may be available
    applyPendingMidiRoutes();

    // NOTE: Window state sync is now handled by PluginWindowManager's timer

    // Update metering from level measurers (runs at 30 FPS on message thread)
    juce::ScopedLock lock(mappingLock_);

    // Update track metering
    for (const auto& [trackId, track] : trackMapping_) {
        if (!track)
            continue;

        // Get the meter client for this track
        auto clientIt = meterClients_.find(trackId);
        if (clientIt == meterClients_.end())
            continue;

        auto& client = clientIt->second;

        MeterData data;

        // Read and clear audio levels from the client (returns DbTimePair)
        auto levelL = client.getAndClearAudioLevel(0);
        auto levelR = client.getAndClearAudioLevel(1);

        // Convert from dB to linear gain (allow > 1.0 for headroom)
        data.peakL = juce::Decibels::decibelsToGain(levelL.dB);
        data.peakR = juce::Decibels::decibelsToGain(levelR.dB);

        // Check for clipping
        data.clipped = data.peakL > 1.0f || data.peakR > 1.0f;

        // RMS would require accumulation over time - simplified for now
        data.rmsL = data.peakL * 0.7f;  // Rough approximation
        data.rmsR = data.peakR * 0.7f;

        meteringBuffer_.pushLevels(trackId, data);
    }

    // Register master meter client with playback context if not done yet
    if (!masterMeterRegistered_) {
        if (auto* ctx = edit_.getCurrentPlaybackContext()) {
            ctx->masterLevels.addClient(masterMeterClient_);
            masterMeterRegistered_ = true;
        }
    }

    // Update master metering from playback context's masterLevels
    if (masterMeterRegistered_) {
        auto levelL = masterMeterClient_.getAndClearAudioLevel(0);
        auto levelR = masterMeterClient_.getAndClearAudioLevel(1);

        // Convert from dB to linear gain
        float peakL = juce::Decibels::decibelsToGain(levelL.dB);
        float peakR = juce::Decibels::decibelsToGain(levelR.dB);

        masterPeakL_.store(peakL, std::memory_order_relaxed);
        masterPeakR_.store(peakR, std::memory_order_relaxed);
    }
}

// =============================================================================
// Plugin Creation Helpers
// =============================================================================

te::Plugin::Ptr AudioBridge::createToneGenerator(te::AudioTrack* track) {
    if (!track)
        return nullptr;

    // Create tone generator plugin via PluginCache
    // ToneGeneratorProcessor will handle parameter configuration
    auto plugin = edit_.getPluginCache().createNewPlugin(te::ToneGeneratorPlugin::xmlTypeName, {});
    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);
        DBG("AudioBridge::createToneGenerator - Created tone generator on track: " +
            track->getName());
        DBG("  Plugin enabled: " << (plugin->isEnabled() ? "YES" : "NO"));
        if (auto* outputDevice = track->getOutput().getOutputDevice(false)) {
            DBG("  Track output device: " + outputDevice->getName());
        } else {
            DBG("  Track output device: NULL!");
        }
    } else {
        DBG("AudioBridge::createToneGenerator - FAILED to create tone generator!");
    }
    return plugin;
}

// Note: createVolumeAndPan removed - track volume is now separate infrastructure
// managed by ensureVolumePluginPosition(), not a user device

te::Plugin::Ptr AudioBridge::createLevelMeter(te::AudioTrack* track) {
    if (!track)
        return nullptr;

    // LevelMeterPlugin has create() that returns ValueTree
    auto plugin = edit_.getPluginCache().createNewPlugin(te::LevelMeterPlugin::create());
    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);
    }
    return plugin;
}

te::Plugin::Ptr AudioBridge::createFourOscSynth(te::AudioTrack* track) {
    if (!track)
        return nullptr;

    // Create 4OSC synthesizer plugin
    auto plugin = edit_.getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);

        // CRITICAL: Increase parameter resolution for all continuous parameters
        // Default is 100 steps which causes stepping artifacts
        // Note: FourOscPlugin exposes many parameters - we'll set high resolution globally
        // for now since distinguishing discrete vs continuous requires deeper inspection
        DBG("FourOscPlugin: Created - parameter resolution will be handled by FourOscProcessor");
    }
    return plugin;
}

te::Plugin::Ptr AudioBridge::loadDeviceAsPlugin(TrackId trackId, const DeviceInfo& device) {
    auto* track = getAudioTrack(trackId);
    if (!track)
        return nullptr;

    DBG("loadDeviceAsPlugin: trackId=" << trackId << " device='" << device.name << "' isInstrument="
                                       << (device.isInstrument ? "true" : "false")
                                       << " format=" << device.getFormatString());

    te::Plugin::Ptr plugin;
    std::unique_ptr<DeviceProcessor> processor;

    if (device.format == PluginFormat::Internal) {
        // Map internal device types to Tracktion plugins and create processors
        if (device.pluginId.containsIgnoreCase("tone")) {
            plugin = createToneGenerator(track);
            if (plugin) {
                processor = std::make_unique<ToneGeneratorProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("4osc")) {
            plugin = createFourOscSynth(track);
            if (plugin) {
                // TODO: Create FourOscProcessor to manage all 4 oscillators + ADSR + filter
                processor = std::make_unique<DeviceProcessor>(device.id, plugin);
            }
            // Note: "volume" devices are NOT created here - track volume is separate infrastructure
            // managed by ensureVolumePluginPosition() and controlled via
            // TrackManager::setTrackVolume()
        } else if (device.pluginId.containsIgnoreCase("meter")) {
            plugin = createLevelMeter(track);
            // No processor for meter - it's just for measurement
        } else if (device.pluginId.containsIgnoreCase("delay")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, {});
            if (plugin)
                track->pluginList.insertPlugin(plugin, -1, nullptr);
        } else if (device.pluginId.containsIgnoreCase("reverb")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::ReverbPlugin::xmlTypeName, {});
            if (plugin)
                track->pluginList.insertPlugin(plugin, -1, nullptr);
        } else if (device.pluginId.containsIgnoreCase("eq")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::EqualiserPlugin::xmlTypeName, {});
            if (plugin)
                track->pluginList.insertPlugin(plugin, -1, nullptr);
        } else if (device.pluginId.containsIgnoreCase("compressor")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::CompressorPlugin::xmlTypeName, {});
            if (plugin)
                track->pluginList.insertPlugin(plugin, -1, nullptr);
        }
    } else {
        // External plugin - find matching description from KnownPluginList
        if (device.uniqueId.isNotEmpty() || device.fileOrIdentifier.isNotEmpty()) {
            // Build PluginDescription from DeviceInfo
            juce::PluginDescription desc;
            desc.name = device.name;
            desc.manufacturerName = device.manufacturer;
            desc.fileOrIdentifier = device.fileOrIdentifier;
            desc.isInstrument = device.isInstrument;

            // Set format
            switch (device.format) {
                case PluginFormat::VST3:
                    desc.pluginFormatName = "VST3";
                    break;
                case PluginFormat::AU:
                    desc.pluginFormatName = "AudioUnit";
                    break;
                case PluginFormat::VST:
                    desc.pluginFormatName = "VST";
                    break;
                default:
                    break;
            }

            // Try to find a matching plugin in KnownPluginList
            DBG("Plugin lookup: searching for name='"
                << device.name << "' manufacturer='" << device.manufacturer
                << "' isInstrument=" << (device.isInstrument ? "true" : "false") << " fileOrId='"
                << device.fileOrIdentifier << "'");

            auto& knownPlugins = engine_.getPluginManager().knownPluginList;

            // Debug: dump all plugins that match the name (case insensitive)
            DBG("  All matching plugins in KnownPluginList:");
            for (const auto& kd : knownPlugins.getTypes()) {
                if (kd.name.containsIgnoreCase(device.name) ||
                    device.name.containsIgnoreCase(kd.name.toStdString())) {
                    DBG("    - name='"
                        << kd.name << "' isInstrument=" << (kd.isInstrument ? "true" : "false")
                        << " fileOrId='" << kd.fileOrIdentifier << "'"
                        << " uniqueId='" << kd.uniqueId << "'"
                        << " identifierString='" << kd.createIdentifierString() << "'");
                }
            }
            bool found = false;
            for (const auto& knownDesc : knownPlugins.getTypes()) {
                // Match by fileOrIdentifier (most specific) BUT also check isInstrument
                // to avoid loading FX when instrument is requested
                if (knownDesc.fileOrIdentifier == device.fileOrIdentifier &&
                    knownDesc.isInstrument == device.isInstrument) {
                    DBG("  -> MATCHED by fileOrIdentifier + isInstrument: " << knownDesc.name);
                    desc = knownDesc;
                    found = true;
                    break;
                }
            }

            // Second pass: match by name, manufacturer, AND isInstrument flag
            if (!found) {
                for (const auto& knownDesc : knownPlugins.getTypes()) {
                    if (knownDesc.name == device.name &&
                        knownDesc.manufacturerName == device.manufacturer &&
                        knownDesc.isInstrument == device.isInstrument) {
                        DBG("  -> MATCHED by name+manufacturer+isInstrument: " << knownDesc.name);
                        desc = knownDesc;
                        found = true;
                        break;
                    }
                }
            }

            // Third pass: match by fileOrIdentifier only (fallback)
            if (!found) {
                for (const auto& knownDesc : knownPlugins.getTypes()) {
                    if (knownDesc.fileOrIdentifier == device.fileOrIdentifier) {
                        DBG("  -> MATCHED by fileOrIdentifier only (fallback): "
                            << knownDesc.name
                            << " isInstrument=" << (knownDesc.isInstrument ? "true" : "false"));
                        desc = knownDesc;
                        found = true;
                        break;
                    }
                }
            }

            if (!found) {
                DBG("  -> NO MATCH FOUND in KnownPluginList!");
            }

            auto result = loadExternalPlugin(trackId, desc);
            if (result.success && result.plugin) {
                plugin = result.plugin;
                auto extProcessor = std::make_unique<ExternalPluginProcessor>(device.id, plugin);
                // Start listening for parameter changes from the plugin's native UI
                extProcessor->startParameterListening();
                processor = std::move(extProcessor);
            } else {
                // Plugin failed to load - notify via callback
                if (onPluginLoadFailed) {
                    onPluginLoadFailed(device.id, result.errorMessage);
                }
                std::cerr << "Plugin load failed for device " << device.id << ": "
                          << result.errorMessage << std::endl;
                return nullptr;  // Don't proceed with a failed plugin
            }
        } else {
            std::cout << "Cannot load external plugin without uniqueId or fileOrIdentifier: "
                      << device.name << std::endl;
        }
    }

    if (plugin) {
        // Store the processor if we created one
        if (processor) {
            // Initialize defaults first if DeviceInfo has no parameters
            // This ensures the plugin starts with sensible values
            if (device.parameters.empty()) {
                if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(processor.get())) {
                    toneProc->initializeDefaults();
                }
            }

            // Sync state from DeviceInfo (only applies if it has values)
            processor->syncFromDeviceInfo(device);

            // Populate parameters back to TrackManager
            DeviceInfo tempInfo;
            processor->populateParameters(tempInfo);
            TrackManager::getInstance().updateDeviceParameters(device.id, tempInfo.parameters);

            deviceProcessors_[device.id] = std::move(processor);
        }

        // Apply device state
        plugin->setEnabled(!device.bypassed);

        // For tone generators (always transport-synced), sync initial state with transport
        if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(processor.get())) {
            // Get current transport state
            bool isPlaying = transportPlaying_.load(std::memory_order_acquire);
            // Bypass if transport is not playing
            toneProc->setBypassed(!isPlaying);
        }

        // If this is an instrument, automatically route all MIDI inputs to this track
        if (device.isInstrument) {
            setTrackMidiInput(trackId, "all");
            std::cout << "Auto-routed MIDI input to track " << trackId
                      << " for instrument: " << device.name << std::endl;
        }

        std::cout << "Loaded device " << device.id << " (" << device.name << ") as plugin"
                  << std::endl;
    }

    return plugin;
}

// =============================================================================
// Mixer Controls
// =============================================================================

void AudioBridge::setTrackVolume(TrackId trackId, float volume) {
    auto* track = getAudioTrack(trackId);
    if (!track)
        return;

    // Use the track's volume plugin (positioned at end of chain before LevelMeter)
    if (auto* volPan = track->getVolumePlugin()) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        volPan->setVolumeDb(db);
    }
}

float AudioBridge::getTrackVolume(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return 1.0f;
    }

    if (auto* volPan = track->getVolumePlugin()) {
        return juce::Decibels::decibelsToGain(volPan->getVolumeDb());
    }
    return 1.0f;
}

void AudioBridge::setTrackPan(TrackId trackId, float pan) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("AudioBridge::setTrackPan - track not found: " << trackId);
        return;
    }

    // Use the track's built-in volume plugin
    if (auto* volPan = track->getVolumePlugin()) {
        volPan->setPan(pan);
    }
}

float AudioBridge::getTrackPan(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return 0.0f;
    }

    if (auto* volPan = track->getVolumePlugin()) {
        return volPan->getPan();
    }
    return 0.0f;
}

void AudioBridge::setMasterVolume(float volume) {
    if (auto masterPlugin = edit_.getMasterVolumePlugin()) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        masterPlugin->setVolumeDb(db);
    }
}

float AudioBridge::getMasterVolume() const {
    if (auto masterPlugin = edit_.getMasterVolumePlugin()) {
        return juce::Decibels::decibelsToGain(masterPlugin->getVolumeDb());
    }
    return 1.0f;
}

void AudioBridge::setMasterPan(float pan) {
    if (auto masterPlugin = edit_.getMasterVolumePlugin()) {
        masterPlugin->setPan(pan);
    }
}

float AudioBridge::getMasterPan() const {
    if (auto masterPlugin = edit_.getMasterVolumePlugin()) {
        return masterPlugin->getPan();
    }
    return 0.0f;
}

// =============================================================================
// Audio Routing
// =============================================================================

void AudioBridge::setTrackAudioOutput(TrackId trackId, const juce::String& destination) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("AudioBridge::setTrackAudioOutput - track not found: " << trackId);
        return;
    }

    DBG("AudioBridge::setTrackAudioOutput - trackId=" << trackId << " destination='" << destination
                                                      << "'");

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

void AudioBridge::setTrackAudioInput(TrackId trackId, const juce::String& deviceId) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("AudioBridge::setTrackAudioInput - track not found: " << trackId);
        return;
    }

    DBG("AudioBridge::setTrackAudioInput - trackId=" << trackId << " deviceId='" << deviceId
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

            if (deviceId == "default" && !allInputs.isEmpty()) {
                // Use first available input device
                auto* firstInput = allInputs.getFirst();
                auto result = firstInput->setTarget(track->itemID, false, nullptr);
                if (result.has_value()) {
                    (*result)->recordEnabled = false;  // Don't auto-enable recording
                    DBG("  -> Routed default input to track");
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

juce::String AudioBridge::getTrackAudioOutput(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return {};
    }

    if (track->isMuted(false)) {
        return {};  // Muted = disabled output
    }

    auto& output = track->getOutput();
    if (output.usesDefaultAudioOut()) {
        return "master";
    }

    // Return the output device name
    return output.getOutputName();
}

juce::String AudioBridge::getTrackAudioInput(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return {};
    }

    // Check if any input device is routed to this track
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (playbackContext) {
        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            auto targets = inputDeviceInstance->getTargets();
            for (auto targetID : targets) {
                if (targetID == track->itemID) {
                    return inputDeviceInstance->owner.getName();
                }
            }
        }
    }

    return {};  // No input assigned
}

// =============================================================================
// MIDI Routing (for live instrument playback)
// =============================================================================

void AudioBridge::enableAllMidiInputDevices() {
    auto& dm = engine_.getDeviceManager();

    // Enable all MIDI input devices at the engine level
    for (auto& midiInput : dm.getMidiInDevices()) {
        if (midiInput && !midiInput->isEnabled()) {
            midiInput->setEnabled(true);
            DBG("Enabled MIDI input device: " << midiInput->getName());
        }
    }

    DBG("All MIDI input devices enabled in Tracktion Engine");
}

void AudioBridge::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        DBG("AudioBridge::setTrackMidiInput - track not found: " << trackId);
        return;
    }

    DBG("AudioBridge::setTrackMidiInput - trackId="
        << trackId << " midiDeviceId='" << midiDeviceId << "' (thread: "
        << (juce::MessageManager::getInstance()->isThisTheMessageThread() ? "message" : "other")
        << ")");

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext) {
        DBG("  -> No playback context available, deferring MIDI routing");
        // Store for later when playback context becomes available
        pendingMidiRoutes_.push_back({trackId, midiDeviceId});
        return;
    }

    DBG("  -> Playback context available, graph allocated: "
        << (playbackContext->isPlaybackGraphAllocated() ? "yes" : "no")
        << ", transport playing: " << (edit_.getTransport().isPlaying() ? "yes" : "no"));

    if (midiDeviceId.isEmpty()) {
        // Disable MIDI input - remove this track as target from all MIDI inputs
        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            // Check if this is a MIDI input device
            if (dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
                if (!result) {
                    DBG("  -> Warning: Could not remove MIDI input target - "
                        << result.getErrorMessage());
                }
            }
        }
        DBG("  -> Cleared MIDI input");
    } else if (midiDeviceId == "all") {
        // Route ALL MIDI input devices to this track
        bool addedAnyRouting = false;
        DBG("  -> Routing ALL MIDI inputs to track. Total inputs in context: "
            << playbackContext->getAllInputs().size());

        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            // Check if this is a MIDI input device
            if (auto* midiDevice =
                    dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
                // Make sure the device is enabled
                if (!midiDevice->isEnabled()) {
                    midiDevice->setEnabled(true);
                }

                // Set monitor mode to "on" so we hear MIDI without needing to arm for recording
                midiDevice->setMonitorMode(te::InputDevice::MonitorMode::on);

                // Set this track as target for live MIDI
                auto result =
                    inputDeviceInstance->setTarget(track->itemID, true, nullptr);  // true = MIDI
                if (result.has_value()) {
                    // Enable monitoring but not recording
                    (*result)->recordEnabled = false;
                    addedAnyRouting = true;
                    DBG("  -> Routed MIDI input '" << midiDevice->getName()
                                                   << "' to track (monitor=on)");
                    DBG("     Device enabled: " << (midiDevice->isEnabled() ? "yes" : "no"));
                    DBG("     Monitor mode: " << (int)midiDevice->getMonitorMode());
                    DBG("     Track name: " << track->getName());
                    DBG("     Track plugins: " << track->pluginList.size());

                    // List plugins on the track for debugging
                    for (int i = 0; i < track->pluginList.size(); ++i) {
                        auto* p = track->pluginList[i];
                        if (p) {
                            DBG("       Plugin " << i << ": " << p->getName() << " (enabled="
                                                 << (p->isEnabled() ? "yes" : "no") << ")");
                        }
                    }
                } else {
                    DBG("  -> FAILED to route MIDI input '" << midiDevice->getName()
                                                            << "' to track");
                }
            }
        }

        // Reallocate the playback graph to include the new MIDI input nodes
        if (addedAnyRouting) {
            if (playbackContext->isPlaybackGraphAllocated()) {
                DBG("  -> Reallocating playback graph to include MIDI input nodes");
                playbackContext->reallocate();
            } else {
                DBG("  -> Playback graph not allocated yet, MIDI routing will take effect on play");
            }
        }
    } else {
        // Route specific MIDI device to this track
        auto& dm = engine_.getDeviceManager();
        bool addedRouting = false;

        // Try to find the device by ID first, then by name
        // Note: JUCE device IDs differ from Tracktion Engine device IDs,
        // so we may need to match by name
        te::MidiInputDevice* midiDevice = nullptr;

        // First try by Tracktion's ID
        if (auto dev = dm.findMidiInputDeviceForID(midiDeviceId)) {
            midiDevice = dev.get();
        } else {
            // Try to find by matching the JUCE device name
            // Get JUCE device name from the identifier
            auto juceDevices = juce::MidiInput::getAvailableDevices();
            juce::String deviceName;
            for (const auto& d : juceDevices) {
                if (d.identifier == midiDeviceId) {
                    deviceName = d.name;
                    break;
                }
            }

            if (deviceName.isNotEmpty()) {
                // Find Tracktion device by name
                for (const auto& device : dm.getMidiInDevices()) {
                    if (device && device->getName() == deviceName) {
                        midiDevice = device.get();
                        DBG("  -> Found device by name: " << deviceName);
                        break;
                    }
                }
            }
        }

        if (midiDevice) {
            if (!midiDevice->isEnabled()) {
                midiDevice->setEnabled(true);
            }

            // Set monitor mode to "on" so we hear MIDI without needing to arm for recording
            midiDevice->setMonitorMode(te::InputDevice::MonitorMode::on);

            // Find the InputDeviceInstance for this MIDI device
            for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                if (&inputDeviceInstance->owner == midiDevice) {
                    auto result = inputDeviceInstance->setTarget(track->itemID, true, nullptr);
                    if (result.has_value()) {
                        (*result)->recordEnabled = false;
                        addedRouting = true;
                        DBG("  -> Routed MIDI input '" << midiDevice->getName()
                                                       << "' to track (monitor=on)");
                        DBG("     Device enabled: " << (midiDevice->isEnabled() ? "yes" : "no"));
                        DBG("     Monitor mode: " << (int)midiDevice->getMonitorMode());
                    } else {
                        DBG("  -> FAILED to route MIDI input '" << midiDevice->getName()
                                                                << "' to track");
                    }
                    break;
                }
            }
        } else {
            DBG("  -> MIDI device not found: " << midiDeviceId);
        }

        // Reallocate the playback graph to include the new MIDI input node
        if (addedRouting) {
            if (playbackContext->isPlaybackGraphAllocated()) {
                DBG("  -> Reallocating playback graph to include MIDI input node");
                playbackContext->reallocate();
            } else {
                DBG("  -> Playback graph not allocated yet, MIDI routing will take effect on play");
            }
        }
    }
}

juce::String AudioBridge::getTrackMidiInput(TrackId trackId) const {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return {};
    }

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext) {
        return {};
    }

    // Check if any MIDI input device is routed to this track
    juce::StringArray midiInputs;
    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        if (dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
            auto targets = inputDeviceInstance->getTargets();
            for (auto targetID : targets) {
                if (targetID == track->itemID) {
                    midiInputs.add(inputDeviceInstance->owner.getName());
                }
            }
        }
    }

    if (midiInputs.isEmpty()) {
        return {};
    } else if (midiInputs.size() == 1) {
        return midiInputs[0];
    } else {
        return "all";  // Multiple inputs = "all"
    }
}

// =============================================================================
// Plugin Editor Windows (delegates to PluginWindowManager)
// =============================================================================

void AudioBridge::showPluginWindow(DeviceId deviceId) {
    if (windowManager_) {
        auto plugin = getPlugin(deviceId);
        if (plugin) {
            // NOLINTNEXTLINE - false positive from static analysis
            windowManager_->showPluginWindow(deviceId, plugin);
        }
    }
}

void AudioBridge::hidePluginWindow(DeviceId deviceId) {
    if (windowManager_) {
        auto plugin = getPlugin(deviceId);
        if (plugin) {
            // NOLINTNEXTLINE - false positive from static analysis
            windowManager_->hidePluginWindow(deviceId, plugin);
        }
    }
}

bool AudioBridge::isPluginWindowOpen(DeviceId deviceId) const {
    if (windowManager_) {
        auto plugin = getPlugin(deviceId);
        if (plugin) {
            return windowManager_->isPluginWindowOpen(plugin);
        }
    }
    return false;
}

bool AudioBridge::togglePluginWindow(DeviceId deviceId) {
    if (windowManager_) {
        auto plugin = getPlugin(deviceId);
        if (plugin) {
            // NOLINTNEXTLINE - false positive from static analysis
            return windowManager_->togglePluginWindow(deviceId, plugin);
        }
    }
    return false;
}

}  // namespace magda
