#include "AudioBridge.hpp"

#include <iostream>
#include <unordered_set>

#include "../core/ClipOperations.hpp"
#include "../engine/PluginWindowManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"
#include "AudioThumbnailManager.hpp"

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

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit)
    : engine_(engine),
      edit_(edit),
      trackController_(engine, edit),
      pluginManager_(engine, edit, trackController_, pluginWindowBridge_, transportState_) {
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

        // Unregister all track meter clients (via trackController)
        trackController_.withTrackMapping([this](const auto& trackMapping) {
            auto& meterClients = trackController_.getMeterClients();
            for (auto& [trackId, track] : trackMapping) {
                if (track) {
                    auto* levelMeter = track->getLevelMeterPlugin();
                    if (levelMeter) {
                        auto it = meterClients.find(trackId);
                        if (it != meterClients.end()) {
                            levelMeter->measurer.removeClient(it->second);
                        }
                    }
                }
            }
        });

        // Clear all mappings - safe now as timer is stopped and lock is held
        trackController_.clearAllMappings();
        pluginManager_.clearAllMappings();
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
    DBG("[BRIDGE-PROP-CHANGED] clipId="
        << clipId << " view=" << (int)clip->view << " startTime=" << clip->startTime << " length="
        << clip->length << " offset=" << clip->offset << " loopStart=" << clip->loopStart
        << " getTeOffset()=" << clip->getTeOffset(clip->loopEnabled));

    if (clip->autoTempo || clip->warpEnabled) {
        DBG("[AUDIO-BRIDGE] clipPropertyChanged clip "
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
            // Clear stale mapping and recreate
            clipIdToEngineId_.erase(it);
            engineIdToClipId_.erase(engineId);
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
    }

    // Update clip position/length
    // CRITICAL: Use preserveSync=true to maintain the content offset
    // When false, Tracktion adjusts the content offset which breaks note playback
    midiClipPtr->setStart(te::TimePosition::fromSeconds(clip->startTime), true, false);
    midiClipPtr->setEnd(te::TimePosition::fromSeconds(clip->startTime + clip->length), false);

    // Force offset to 0 to ensure notes play from clip start
    midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

    // Set up internal looping on the TE clip
    if (clip->loopEnabled) {
        // For MIDI clips, use clip length as loop region
        const double beatsPerSecond = edit_.tempoSequence.getBpmAt(te::TimePosition()) / 60.0;
        double clipLengthBeats = clip->length * beatsPerSecond;
        auto& tempoSeq = edit_.tempoSequence;
        auto loopStartTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(0.0));
        auto loopEndTime = tempoSeq.beatsToTime(te::BeatPosition::fromBeats(clipLengthBeats));

        midiClipPtr->setLoopRange(te::TimeRange(loopStartTime, loopEndTime));
        midiClipPtr->setLoopRangeBeats(
            {te::BeatPosition::fromBeats(0.0), te::BeatPosition::fromBeats(clipLengthBeats)});
    } else {
        midiClipPtr->disableLooping();
    }

    // Clear existing notes and rebuild from ClipManager
    auto& sequence = midiClipPtr->getSequence();
    sequence.clear(nullptr);

    // Calculate the beat range visible in this clip based on midiOffset
    const double beatsPerSecond = 2.0;  // TODO: Get from tempo
    double clipLengthBeats = clip->length * beatsPerSecond;
    // Only session clips use midiOffset; arrangement clips play all their notes
    double effectiveOffset =
        (clip->view == ClipView::Session || clip->loopEnabled) ? clip->midiOffset : 0.0;
    double visibleStart = effectiveOffset;  // Where the clip's "view window" starts
    double visibleEnd = effectiveOffset + clipLengthBeats;

    DBG("MIDI SYNC clip " << clipId << ":");
    DBG("  midiOffset=" << clip->midiOffset << ", clipLength=" << clipLengthBeats << " beats");
    DBG("  loopEnabled=" << (int)clip->loopEnabled);
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

        // When looping, truncate notes at clip boundary to prevent stuck notes
        double adjustedLength = note.lengthBeats;
        if (clip->loopEnabled) {
            if (noteStart >= clipLengthBeats)
                continue;
            if (noteEnd > clipLengthBeats)
                adjustedLength = clipLengthBeats - noteStart;
        }

        // Calculate position relative to clip start (subtract midiOffset for session clips only)
        double adjustedStart = noteStart - effectiveOffset;

        // Truncate note if it starts before the visible range
        if (adjustedStart < 0.0) {
            adjustedLength = noteEnd - visibleStart;
            adjustedStart = 0.0;
        }

        // Truncate note if it extends past the visible range
        if (adjustedStart + adjustedLength > clipLengthBeats) {
            adjustedLength = clipLengthBeats - adjustedStart;
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
        if (clip->audioFilePath.isEmpty()) {
            DBG("AudioBridge: No audio file for clip " << clipId);
            return;
        }
        juce::File audioFile(clip->audioFilePath);
        if (!audioFile.existsAsFile()) {
            DBG("AudioBridge: Audio file not found: " << clip->audioFilePath);
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
            DBG("AudioBridge: Failed to create WaveAudioClip");
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
        clipIdToEngineId_[clipId] = engineClipId;
        engineIdToClipId_[engineClipId] = clipId;

        DBG("AudioBridge: Created WaveAudioClip (engine ID: " << engineClipId << ")");
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
        if (clip->audioFilePath.isEmpty())
            return false;

        juce::File audioFile(clip->audioFilePath);
        if (!audioFile.existsAsFile()) {
            DBG("AudioBridge::syncSessionClipToSlot: Audio file not found: "
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

        // Force offset to 0
        midiClipPtr->setOffset(te::TimeDuration::fromSeconds(0.0));

        // Add MIDI notes (skip/truncate at loop boundary to prevent stuck notes)
        auto& sequence = midiClipPtr->getSequence();
        double bpm = edit_.tempoSequence.getBpmAt(te::TimePosition());
        double srcLength = clip->getSourceLength();
        double loopStartBeat = clip->loopStart * (bpm / 60.0);
        double loopLengthBeats = srcLength * (bpm / 60.0);
        double loopEndBeat = loopStartBeat + loopLengthBeats;

        for (const auto& note : clip->midiNotes) {
            double start = note.startBeat;
            double length = note.lengthBeats;

            if (clip->loopEnabled && loopLengthBeats > 0.0) {
                if (start >= loopEndBeat)
                    continue;
                double noteEnd = start + length;
                if (noteEnd > loopEndBeat)
                    length = loopEndBeat - start;
            }

            sequence.addNote(note.noteNumber, te::BeatPosition::fromBeats(start),
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

void AudioBridge::stopSessionClip(ClipId clipId) {
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
        auto* audioTrack = getAudioTrack(clip->trackId);
        if (audioTrack) {
            for (auto* plugin : audioTrack->pluginList) {
                if (plugin->isSynth()) {
                    plugin->reset();
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

te::Clip* AudioBridge::getArrangementTeClip(ClipId clipId) const {
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
// Plugin Loading
// =============================================================================

te::Plugin::Ptr AudioBridge::loadBuiltInPlugin(const TrackId trackId, const juce::String& type) {
    return pluginManager_.loadBuiltInPlugin(trackId, type);
}

PluginLoadResult AudioBridge::loadExternalPlugin(TrackId trackId,
                                                 const juce::PluginDescription& description) {
    return pluginManager_.loadExternalPlugin(trackId, description);
}

te::Plugin::Ptr AudioBridge::addLevelMeterToTrack(TrackId trackId) {
    return pluginManager_.addLevelMeterToTrack(trackId);
}

void AudioBridge::ensureVolumePluginPosition(te::AudioTrack* track) const {
    pluginManager_.ensureVolumePluginPosition(track);
}

// =============================================================================
// Track Mapping
// =============================================================================

te::AudioTrack* AudioBridge::getAudioTrack(TrackId trackId) const {
    return trackController_.getAudioTrack(trackId);
}

te::Plugin::Ptr AudioBridge::getPlugin(DeviceId deviceId) const {
    return pluginManager_.getPlugin(deviceId);
}

DeviceProcessor* AudioBridge::getDeviceProcessor(DeviceId deviceId) const {
    return pluginManager_.getDeviceProcessor(deviceId);
}

te::AudioTrack* AudioBridge::createAudioTrack(TrackId trackId, const juce::String& name) {
    return trackController_.createAudioTrack(trackId, name);
}

void AudioBridge::removeAudioTrack(TrackId trackId) {
    trackController_.removeAudioTrack(trackId);
}

// =============================================================================
// Parameter Queue
// =============================================================================

bool AudioBridge::pushParameterChange(DeviceId deviceId, int paramIndex, float value) {
    // Delegate to ParameterManager
    return parameterManager_.pushChange(deviceId, paramIndex, value);
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
    pluginManager_.syncTrackPlugins(trackId);

    // Auto-route MIDI for instruments (coordination logic)
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (trackInfo) {
        for (const auto& element : trackInfo->chainElements) {
            if (std::holds_alternative<DeviceInfo>(element)) {
                const auto& device = std::get<DeviceInfo>(element);
                if (device.isInstrument) {
                    setTrackMidiInput(trackId, "all");
                    break;
                }
            }
        }
    }
}

void AudioBridge::ensureTrackMapping(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (trackInfo) {
        trackController_.ensureTrackMapping(trackId, trackInfo->name);
    }
}

// =============================================================================
// Audio Callback Support
// =============================================================================

void AudioBridge::processParameterChanges() {
    MAGDA_MONITOR_SCOPE("ParamChanges");

    ParameterChange change;
    while (parameterManager_.popChange(change)) {
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
    // Delegate to TransportStateManager
    transportState_.updateState(isPlaying, justStarted, justLooped);

    // Enable/disable tone generators based on transport state (via PluginManager)
    pluginManager_.updateTransportSyncedProcessors(isPlaying);
}

// =============================================================================
// MIDI Activity Monitoring
// =============================================================================

// Methods moved to inline implementations in AudioBridge.hpp

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

    // Poll for reversed proxy file completion
    if (pendingReverseClipId_ != INVALID_CLIP_ID) {
        auto it = clipIdToEngineId_.find(pendingReverseClipId_);
        if (it != clipIdToEngineId_.end()) {
            for (auto* track : te::getAudioTracks(edit_)) {
                for (auto* teClip : track->getClips()) {
                    if (teClip->itemID.toString().toStdString() == it->second) {
                        if (auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip)) {
                            auto proxyFile = audioClip->getPlaybackFile().getFile();
                            if (proxyFile.existsAsFile()) {
                                DBG("REVERSE TIMER: proxy ready — reallocating ("
                                    << proxyFile.getFullPathName() << ")");
                                pendingReverseClipId_ = INVALID_CLIP_ID;
                                if (auto* ctx = edit_.getCurrentPlaybackContext())
                                    ctx->reallocate();
                            }
                        }
                        break;
                    }
                }
            }
        } else {
            pendingReverseClipId_ = INVALID_CLIP_ID;
        }
    }

    // NOTE: Window state sync is now handled by PluginWindowManager's timer

    // Update metering from level measurers (runs at 30 FPS on message thread)
    // Use trackController's withTrackMapping for thread-safe access
    trackController_.withTrackMapping([this](const auto& trackMapping) {
        auto& meterClients = trackController_.getMeterClients();

        // Update track metering
        for (const auto& [trackId, track] : trackMapping) {
            if (!track)
                continue;

            // Get the meter client for this track
            auto clientIt = meterClients.find(trackId);
            if (clientIt == meterClients.end())
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
    });

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
// Mixer Controls
// =============================================================================

void AudioBridge::setTrackVolume(TrackId trackId, float volume) {
    trackController_.setTrackVolume(trackId, volume);
}

float AudioBridge::getTrackVolume(TrackId trackId) const {
    return trackController_.getTrackVolume(trackId);
}

void AudioBridge::setTrackPan(TrackId trackId, float pan) {
    trackController_.setTrackPan(trackId, pan);
}

float AudioBridge::getTrackPan(TrackId trackId) const {
    return trackController_.getTrackPan(trackId);
}

void AudioBridge::setMasterVolume(float volume) {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        masterPlugin->setVolumeDb(db);
    }
}

float AudioBridge::getMasterVolume() const {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin) {
        return juce::Decibels::decibelsToGain(masterPlugin->getVolumeDb());
    }
    return 1.0f;
}

void AudioBridge::setMasterPan(float pan) {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin) {
        masterPlugin->setPan(pan);
    }
}

float AudioBridge::getMasterPan() const {
    auto masterPlugin = edit_.getMasterVolumePlugin();
    if (masterPlugin) {
        return masterPlugin->getPan();
    }
    return 0.0f;
}

// =============================================================================
// Audio Routing
// =============================================================================

void AudioBridge::setTrackAudioOutput(TrackId trackId, const juce::String& destination) {
    trackController_.setTrackAudioOutput(trackId, destination);
}

void AudioBridge::setTrackAudioInput(TrackId trackId, const juce::String& deviceId) {
    trackController_.setTrackAudioInput(trackId, deviceId);
}

juce::String AudioBridge::getTrackAudioOutput(TrackId trackId) const {
    return trackController_.getTrackAudioOutput(trackId);
}

juce::String AudioBridge::getTrackAudioInput(TrackId trackId) const {
    return trackController_.getTrackAudioInput(trackId);
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
    auto plugin = getPlugin(deviceId);
    if (plugin) {
        pluginWindowBridge_.showPluginWindow(deviceId, plugin);
    }
}

void AudioBridge::hidePluginWindow(DeviceId deviceId) {
    auto plugin = getPlugin(deviceId);
    if (plugin) {
        pluginWindowBridge_.hidePluginWindow(deviceId, plugin);
    }
}

bool AudioBridge::isPluginWindowOpen(DeviceId deviceId) const {
    auto plugin = getPlugin(deviceId);
    if (plugin) {
        return pluginWindowBridge_.isPluginWindowOpen(plugin);
    }
    return false;
}

bool AudioBridge::togglePluginWindow(DeviceId deviceId) {
    auto plugin = getPlugin(deviceId);
    if (plugin) {
        return pluginWindowBridge_.togglePluginWindow(deviceId, plugin);
    }
    return false;
}

// ============================================================================
// Transient Detection
// ============================================================================

bool AudioBridge::getTransientTimes(ClipId clipId) {
    return warpMarkerManager_.getTransientTimes(edit_, clipIdToEngineId_, clipId);
}

// =============================================================================
// Warp Markers (delegated to WarpMarkerManager)
// =============================================================================

void AudioBridge::enableWarp(ClipId clipId) {
    warpMarkerManager_.enableWarp(edit_, clipIdToEngineId_, clipId);
}

void AudioBridge::disableWarp(ClipId clipId) {
    warpMarkerManager_.disableWarp(edit_, clipIdToEngineId_, clipId);
}

std::vector<WarpMarkerInfo> AudioBridge::getWarpMarkers(ClipId clipId) {
    return warpMarkerManager_.getWarpMarkers(edit_, clipIdToEngineId_, clipId);
}

int AudioBridge::addWarpMarker(ClipId clipId, double sourceTime, double warpTime) {
    return warpMarkerManager_.addWarpMarker(edit_, clipIdToEngineId_, clipId, sourceTime, warpTime);
}

double AudioBridge::moveWarpMarker(ClipId clipId, int index, double newWarpTime) {
    return warpMarkerManager_.moveWarpMarker(edit_, clipIdToEngineId_, clipId, index, newWarpTime);
}

void AudioBridge::removeWarpMarker(ClipId clipId, int index) {
    warpMarkerManager_.removeWarpMarker(edit_, clipIdToEngineId_, clipId, index);
}

}  // namespace magda
