#include "AudioBridge.hpp"

#include <unordered_set>

#include "../core/AutomationManager.hpp"
#include "../core/ClipOperations.hpp"
#include "../core/ModulatorEngine.hpp"
#include "../core/RackInfo.hpp"
#include "../core/controllers/ControllerRegistry.hpp"
#include "../engine/PluginWindowManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"
#include "AudioThumbnailManager.hpp"
#include "midi/MidiDeviceMatch.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/SidechainTriggerBus.hpp"
#include "session/SessionMonitorPlugin.hpp"

namespace magda {

namespace {

/**
 * @brief Recursively search chain elements for a DeviceInfo with the given ID
 *
 * Searches top-level devices and recurses into RackInfo.chains[].elements[].
 */
const DeviceInfo* findDeviceRecursive(const std::vector<ChainElement>& elements,
                                      DeviceId deviceId) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            if (device.id == deviceId) {
                return &device;
            }
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            for (const auto& chain : rack.chains) {
                auto* found = findDeviceRecursive(chain.elements, deviceId);
                if (found)
                    return found;
            }
        }
    }
    return nullptr;
}

te::InputDevice::MonitorMode toTeMonitorMode(InputMonitorMode mode) {
    switch (mode) {
        case InputMonitorMode::In:
            return te::InputDevice::MonitorMode::on;
        case InputMonitorMode::Auto:
            return te::InputDevice::MonitorMode::automatic;
        case InputMonitorMode::Off:
        default:
            return te::InputDevice::MonitorMode::off;
    }
}

}  // namespace

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit)
    : engine_(engine),
      edit_(edit),
      trackController_(engine, edit),
      pluginManager_(engine, edit, trackController_, pluginWindowBridge_, transportState_),
      clipSynchronizer_(edit, trackController_, warpMarkerManager_),
      automationPlayback_(*this, edit),
      automationRecording_(edit) {
    // Wire up async plugin load completion callback to notify UI
    pluginManager_.onAsyncPluginLoaded = [](TrackId trackId) {
        TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
    };

    // Re-establish MIDI routing and input monitor state after graph reallocate
    clipSynchronizer_.onGraphReallocated = [this]() {
        updateMidiRoutingForSelection();
        resyncAllInputMonitors();
    };

    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Hook into ModulatorEngine's per-tick callback so that after the visual
    // sim updates each ModInfo, we overlay TE's authoritative LFO phase +
    // value. Single source of truth — no drift between UI marker and audio.
    // The hook is cleared in the destructor before this AudioBridge dies.
    ModulatorEngine::getInstance().setPostUpdateHook(
        [this]() { pluginManager_.syncLFOValuesToVisuals(); });

    // Set up per-device metering manager
    deviceMetering_.setPluginManager(&pluginManager_);
    DeviceMeteringManager::registerForEdit(edit_, &deviceMetering_);

    // Master metering will be registered when playback context is available
    // (done in timerCallback when context exists)

    // Start timer for metering updates (30 FPS for smooth UI)
    startTimerHz(30);

    DBG("AudioBridge initialized");
}

AudioBridge::~AudioBridge() {
    DBG("AudioBridge::~AudioBridge - starting cleanup");

    // CRITICAL: Acquire lock BEFORE stopping timer to ensure proper synchronization.
    // This prevents race condition where timerCallback() could be running while
    // we're destroying member variables. By holding the lock across stopTimer(),
    // we guarantee that any running timer callback completes before destruction.
    {
        juce::ScopedLock lock(mappingLock_);

        // Set shutdown flag while holding lock to prevent new timer operations
        isShuttingDown_.store(true, std::memory_order_release);

        // Drop the ModulatorEngine hook BEFORE we tear down anything it
        // captures (this AudioBridge → pluginManager_). The engine timer
        // may fire concurrently on the message thread until stopped.
        ModulatorEngine::getInstance().setPostUpdateHook(nullptr);

        // Stop timer while holding lock - ensures no callback is running when we proceed
        stopTimer();

        // Now safe to remove listeners as timer is stopped and shutdown flag is set
        TrackManager::getInstance().removeListener(this);
        // Note: ClipManager listener removed by ClipSynchronizer destructor

        // NOTE: Plugin windows are now closed by PluginWindowManager BEFORE AudioBridge
        // is destroyed (in TracktionEngineWrapper::shutdown()). No window cleanup needed here.

        // Unregister per-device metering from Edit
        DeviceMeteringManager::unregisterForEdit(edit_);
        deviceMetering_.clear();

        // Unregister master meter client from playback context
        if (masterMeterRegistered_) {
            if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                ctx->masterLevels.removeClient(masterMeterClient_);
            }
        }

        // Unregister all track meter clients (via trackController)
        trackController_.withTrackMapping([this](const auto& trackMapping) {
            for (auto& [trackId, track] : trackMapping) {
                trackController_.removeMeterClient(trackId);
            }
        });

        // Clear baked automation curves BEFORE PluginManager mappings are
        // wiped. Once mappings are gone, target resolution can't find any
        // macro/mod parameter, so the curves would stay populated and trip
        // a TE assert when MacroParameters / Modifiers destruct in Edit
        // teardown.
        automationPlayback_.clearAllLanes();

        // Clear all mappings - safe now as timer is stopped and lock is held
        trackController_.clearAllMappings();
        pluginManager_.clearAllMappings();
    }

    DBG("AudioBridge destroyed");
}

// =============================================================================
// TrackManagerListener implementation
// =============================================================================

void AudioBridge::tracksChanged() {
    // Tracks were added/removed/reordered - sync all
    syncAll();

    // Post-load record-arm sync: TrackInfo::recordArmed is persisted across
    // project save/load, but trackPropertyChanged only runs when the *value*
    // changes. Without this pass, a project saved with an armed track reopens
    // with the flag set but TE's InputDeviceInstance destinations still have
    // recordEnabled=N, and transport.record() silently produces no clip. Also
    // covers the add/remove/reorder paths — cheap and idempotent.
    syncAllArmedTracksToTE();
}

void AudioBridge::syncAllArmedTracksToTE() {
    // Iterate *every* track (not just armed ones) so TE's destination
    // recordEnabled flags reconcile in both directions. syncRecordArmedToTE
    // no-ops while the transport is playing; on stop we need to push the
    // current TrackInfo::recordArmed state — including Ns — so TE doesn't
    // keep a stale recordEnabled=Y on a now-disarmed track and silently
    // record into it.
    for (const auto& trackInfo : TrackManager::getInstance().getTracks())
        syncRecordArmedToTE(trackInfo.id);
}

void AudioBridge::syncRecordArmedToTE(TrackId trackId) {
    auto* track = getAudioTrack(trackId);
    if (!track)
        return;
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    // Only sync when transport is stopped — if the transport is already
    // playing (e.g. session clips running), calling setRecordingEnabled()
    // causes TE to start recording immediately, capturing the session
    // clip's own MIDI output as "ghost notes". The armed state is stored in
    // TrackInfo and will be picked up when transport.record() is called.
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext || edit_.getTransport().isPlaying())
        return;

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        auto targets = inputDeviceInstance->getTargets();
        for (auto targetID : targets) {
            if (targetID == track->itemID) {
                inputDeviceInstance->setRecordingEnabled(track->itemID, trackInfo->recordArmed);
                break;
            }
        }
    }
}

void AudioBridge::trackPropertyChanged(int trackId) {
    // Track property changed (volume, pan, mute, solo, recordArmed) - sync to Tracktion Engine
    auto* track = getAudioTrack(trackId);
    if (track) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (trackInfo) {
            // Sync mute/solo to track
            track->setMute(trackInfo->muted);
            track->setSolo(trackInfo->soloed);

            // Sync freeze state
            if (trackInfo->frozen != track->isFrozen(te::AudioTrack::individualFreeze)) {
                DBG("AudioBridge::trackPropertyChanged - freeze sync: trackId="
                    << trackId << " frozen=" << (trackInfo->frozen ? 1 : 0) << " teTrackName="
                    << track->getName() << " numClips=" << track->getClips().size()
                    << " trackLength=" << track->getLength().inSeconds());
                track->setFrozen(trackInfo->frozen, te::AudioTrack::individualFreeze);
            }

            // Sync volume/pan to VolumeAndPanPlugin.
            //
            // Skip only when this callback is re-entering from
            // AutomationPlaybackEngine echoing a curve value back into
            // TrackInfo. Pushing that value into TE would race with TE's
            // own curve evaluation. Manual fader/pan edits on any track —
            // including tracks without automation — still flow through,
            // even while transport is playing.
            if (!AutomationManager::getInstance().isApplyingAutomationWrite()) {
                setTrackVolume(trackId, trackInfo->volume);
                setTrackPan(trackId, trackInfo->pan);
            }

            // Sync audio output routing
            trackController_.setTrackAudioOutput(trackId, trackInfo->audioOutputDevice);

            // Sync rack/chain volume and pan
            pluginManager_.syncRackProperties(trackId);

            // Sync send levels to AuxSendPlugins. Skipped while an automation
            // writeback is in progress so a baked send curve isn't clobbered
            // by stale TrackInfo values (same invariant as TrackVolume/Pan
            // above — the AuxSend gain parameter is TE-automated; MAGDA's
            // TrackInfo::sends[i].level tracks it via setSendLevel and must
            // not be written back on top of a running curve).
            if (!AutomationManager::getInstance().isApplyingAutomationWrite()) {
                for (const auto& send : trackInfo->sends) {
                    if (auto* auxSend = track->getAuxSendPlugin(send.busIndex)) {
                        auxSend->setGainDb(juce::Decibels::gainToDecibels(send.level));
                    }
                }
            }

            // Sync input monitor mode to TE InputDevice instances
            {
                auto* playbackContext = edit_.getCurrentPlaybackContext();
                if (playbackContext) {
                    auto teMode = toTeMonitorMode(trackInfo->inputMonitor);
                    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                        auto targets = inputDeviceInstance->getTargets();
                        for (auto targetID : targets) {
                            if (targetID == track->itemID) {
                                inputDeviceInstance->owner.setMonitorMode(teMode);
                                break;
                            }
                        }
                    }
                }
            }

            // Update MIDI routing when record arm changes
            // (armed tracks should receive MIDI even when not selected)
            updateMidiRoutingForSelection();

            syncRecordArmedToTE(trackId);
        }
    }

    // Forward to automation recording engine
    automationRecording_.onTrackPropertyChanged(trackId);
}

void AudioBridge::trackSelectionChanged(TrackId newTrackId) {
    if (newTrackId == lastSelectedTrack_)
        return;
    lastSelectedTrack_ = newTrackId;
    updateMidiRoutingForSelection();
}

void AudioBridge::updateMidiRoutingForSelection() {
    auto& tm = TrackManager::getInstance();
    const auto& tracks = tm.getTracks();

    // Determine which track should receive MIDI: the selected track,
    // or the track owning the selected clip (clip selection clears track selection)
    TrackId midiTrackId = lastSelectedTrack_;
    if (midiTrackId != INVALID_TRACK_ID) {
        const auto* selectedTrack = tm.getTrack(midiTrackId);
        if (selectedTrack && selectedTrack->type == TrackType::MultiOut &&
            selectedTrack->hasParent()) {
            midiTrackId = selectedTrack->parentId;
        }
    }
    if (midiTrackId == INVALID_TRACK_ID) {
        auto selectedClipId = ClipManager::getInstance().getSelectedClip();
        if (selectedClipId != INVALID_CLIP_ID) {
            if (auto* clip = ClipManager::getInstance().getClip(selectedClipId))
                midiTrackId = clip->trackId;
        }
    }

    for (const auto& track : tracks) {
        // Aux tracks never receive MIDI
        if (track.type == TrackType::Aux)
            continue;

        bool shouldReceiveMidi = (track.id == midiTrackId) || track.recordArmed;

        // Check if this track needs MIDI (has an instrument or a MIDI-triggered mod)
        // Recurse into racks to find instruments/mods inside rack chains
        bool needsMidi = false;
        std::function<bool(const std::vector<ChainElement>&)> checkElements;
        checkElements = [&](const std::vector<ChainElement>& elements) -> bool {
            for (const auto& element : elements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    if (device.isInstrument)
                        return true;
                    // Chord Engine needs live MIDI input for real-time detection
                    if (device.pluginId.containsIgnoreCase(
                            daw::audio::MidiChordEnginePlugin::xmlTypeName))
                        return true;
                    for (const auto& mod : device.mods) {
                        if (mod.enabled && mod.triggerMode == LFOTriggerMode::MIDI)
                            return true;
                    }
                } else if (isRack(element)) {
                    const auto& rack = getRack(element);
                    // Check rack-level mods
                    for (const auto& mod : rack.mods) {
                        if (mod.enabled && mod.triggerMode == LFOTriggerMode::MIDI)
                            return true;
                    }
                    // Recurse into rack chains
                    for (const auto& chain : rack.chains) {
                        if (checkElements(chain.elements))
                            return true;
                    }
                }
            }
            return false;
        };
        needsMidi = checkElements(track.chainElements);

        // Record-armed tracks always need MIDI routing, even without an instrument
        if (!needsMidi && !track.recordArmed)
            continue;

        // Check current MIDI routing state
        juce::String currentMidi = getTrackMidiInput(track.id);
        bool currentlyRouted = currentMidi.isNotEmpty();

        if (shouldReceiveMidi && !currentlyRouted) {
            setTrackMidiInput(track.id, "all");
        } else if (!shouldReceiveMidi && currentlyRouted) {
            setTrackMidiInput(track.id, "");
        }
    }

    // setTrackMidiInput already handles reallocate() internally
}

void AudioBridge::resyncAllInputMonitors() {
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    auto& tm = TrackManager::getInstance();

    // For each input device, determine the desired monitor mode by aggregating
    // across all of its target tracks:
    // - on        if any target track is In
    // - automatic if any target track is Auto (and none are In)
    // - off       otherwise
    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        bool anyIn = false;
        bool anyAuto = false;

        for (auto targetID : inputDeviceInstance->getTargets()) {
            for (const auto& trackInfo : tm.getTracks()) {
                auto* track = trackController_.getAudioTrack(trackInfo.id);
                if (!track || targetID != track->itemID)
                    continue;

                switch (trackInfo.inputMonitor) {
                    case InputMonitorMode::In:
                        anyIn = true;
                        break;
                    case InputMonitorMode::Auto:
                        anyAuto = true;
                        break;
                    case InputMonitorMode::Off:
                        break;
                }
                break;  // Found the matching track for this targetID
            }
        }

        auto teMode = te::InputDevice::MonitorMode::off;
        if (anyIn)
            teMode = te::InputDevice::MonitorMode::on;
        else if (anyAuto)
            teMode = te::InputDevice::MonitorMode::automatic;

        inputDeviceInstance->owner.setMonitorMode(teMode);
    }
}

void AudioBridge::trackDevicesChanged(TrackId trackId) {
    DBG("AudioBridge::trackDevicesChanged: trackId=" << trackId);
    // Devices on a track changed - resync that track's plugins
    syncTrackPlugins(trackId);
}

void AudioBridge::deviceModifiersChanged(TrackId trackId) {
    // Skip the modifier resync when this notify is the playback engine
    // echoing a baked curve value (e.g. LFO rate) back into MAGDA state.
    // TE already drove the modifier param on the audio thread; resyncing
    // would just push the same value back through and fight the curve.
    if (AutomationManager::getInstance().isApplyingAutomationWrite())
        return;

    // Modifier properties changed (rate, waveform, sync, trigger mode) - resync only modifiers
    pluginManager_.resyncDeviceModifiers(trackId);

    // Mod-rate lanes are mode-aware: tempoSync flips swap the bake target
    // between TE's `rate` (Hz) and `rateType` (sync division). Force a rebake
    // and broadcast lane-property change so any visible Rate lane recomputes
    // its ParameterInfo (scale, labels, range) and repaints. Cheap to nudge
    // unconditionally — most modifier changes (waveform, trigger mode, rate
    // value) don't need this, but the rebake is coalesced via needsRebake_.
    auto& autoMgr = AutomationManager::getInstance();
    for (const auto& lane : autoMgr.getLanes()) {
        if (lane.target.kind == ControlTarget::Kind::ModParam &&
            lane.target.devicePath.trackId == trackId) {
            autoMgr.invalidateLane(lane.id);
        }
    }

    // Re-check sidechain monitors on this track and all other tracks
    // (a sidechain source change on this track may affect the source track's monitor)
    for (const auto& track : magda::TrackManager::getInstance().getTracks()) {
        pluginManager_.checkSidechainMonitor(track.id);
        pluginManager_.checkAudioSidechainMonitor(track.id);
    }

    // Re-check MIDI routing in case trigger mode changed to/from MIDI
    updateMidiRoutingForSelection();
}

void AudioBridge::audioSidechainTriggered(TrackId /*sourceTrackId*/) {
    // Audio sidechain triggering now happens on the audio thread via
    // AudioSidechainMonitorPlugin — this callback is no longer needed.
}

void AudioBridge::modParameterChanged(TrackId trackId, const ChainNodePath& devicePath, ModId modId,
                                      int paramIndex, float value) {
    // Skip recording when this notify is the playback engine echoing a baked
    // curve back into MAGDA state — it would create circular lane writes.
    if (AutomationManager::getInstance().isApplyingAutomationWrite())
        return;
    automationRecording_.onModParameterValueChanged(trackId, devicePath, modId, paramIndex, value);
}

void AudioBridge::macroValueChanged(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                                    float value) {
    // Skip the TE writeback when this notify is the playback engine echoing
    // a baked curve value back into MAGDA state — TE already drove the
    // MacroParameter on the audio thread, re-pushing fights its own curve.
    // Manual user edits (no AutomationWriteScope) still flow through.
    if (!AutomationManager::getInstance().isApplyingAutomationWrite())
        pluginManager_.setMacroValue(trackId, scope, ownerId, macroIndex, value);
    automationRecording_.onMacroValueChanged(trackId, scope, ownerId, macroIndex, value);
}

void AudioBridge::masterChannelChanged() {
    // Master channel property changed - sync to Tracktion Engine
    const auto& master = TrackManager::getInstance().getMasterChannel();
    setMasterPan(master.pan);

    // When muted, set volume to silence; otherwise use the actual volume
    if (master.muted) {
        setMasterVolume(0.0f);
    } else {
        setMasterVolume(master.volume);
    }
}

void AudioBridge::deviceParameterChanged(DeviceId deviceId, int paramIndex, float newValue) {
    // A single device parameter changed - sync only that parameter to processor
    auto* processor = getDeviceProcessor(deviceId);
    if (!processor) {
        return;
    }

    // Use setParameterByIndex for efficient single-param sync
    if (auto* extProcessor = dynamic_cast<ExternalPluginProcessor*>(processor)) {
        extProcessor->setParameterByIndex(paramIndex, newValue);
    } else if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(processor)) {
        toneProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* samplerProc = dynamic_cast<MagdaSamplerProcessor*>(processor)) {
        samplerProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* fourOscProc = dynamic_cast<FourOscProcessor*>(processor)) {
        fourOscProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* eqProc = dynamic_cast<EqualiserProcessor*>(processor)) {
        eqProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* compProc = dynamic_cast<CompressorProcessor*>(processor)) {
        compProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* reverbProc = dynamic_cast<ReverbProcessor*>(processor)) {
        reverbProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* delayProc = dynamic_cast<DelayProcessor*>(processor)) {
        delayProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* chorusProc = dynamic_cast<ChorusProcessor*>(processor)) {
        chorusProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* phaserProc = dynamic_cast<PhaserProcessor*>(processor)) {
        phaserProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* lpProc = dynamic_cast<FilterProcessor*>(processor)) {
        lpProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* pitchProc = dynamic_cast<PitchShiftProcessor*>(processor)) {
        pitchProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* irProc = dynamic_cast<ImpulseResponseProcessor*>(processor)) {
        irProc->setParameterByIndex(paramIndex, newValue);
    } else if (auto* utilityProc = dynamic_cast<UtilityProcessor*>(processor)) {
        utilityProc->setParameterByIndex(paramIndex, newValue);
    }

    // Forward to automation recording engine
    automationRecording_.onDeviceParameterChanged(deviceId, paramIndex, newValue);
}

void AudioBridge::devicePropertyChanged(DeviceId deviceId) {
    // A device property changed (gain, bypass, etc.) - sync to processor
    auto* processor = getDeviceProcessor(deviceId);

    // Find the DeviceInfo to get updated values
    // Search through all tracks, recursing into racks
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        auto* device = findDeviceRecursive(track.chainElements, deviceId);
        if (device) {
            if (processor) {
                processor->syncFromDeviceInfo(*device);
            } else {
                // For plugins without a processor (e.g. Chord Engine), sync bypass directly
                auto tePlugin = pluginManager_.getPlugin(deviceId);
                if (tePlugin)
                    tePlugin->setEnabled(!device->bypassed);
            }

            // Wrapped instruments consume MIDI while active. When bypassed, disable the
            // wrapper rack itself so TE skips it and passes MIDI to later devices.
            if (auto* rackInstance =
                    pluginManager_.getInstrumentRackManager().getRackInstance(deviceId)) {
                rackInstance->setEnabled(!device->bypassed);
            }

            // Push gain to the audio-graph atomic so DeviceGainNode picks it up.
            // DeviceGainNode sits OUTSIDE the plugin in the TE graph (between the plugin and
            // the level meter), so it stays active even when the plugin is bypassed. Force
            // unity while bypassed so the slider stops attenuating signal that isn't going
            // through the plugin (#1189). The user's gainValue is preserved on DeviceInfo
            // and gets re-pushed when the device is re-enabled.
            deviceMetering_.setGain(deviceId, device->bypassed ? 1.0f : device->gainValue);

            // When bypass changes, resync modifiers so they are removed/restored
            pluginManager_.resyncDeviceModifiers(track.id);

            // Sync sidechain routing if changed
            auto* tePlugin = pluginManager_.getPlugin(deviceId).get();
            if (tePlugin && tePlugin->canSidechain()) {
                if (device->sidechain.isActive() &&
                    device->sidechain.type == SidechainConfig::Type::Audio) {
                    auto* sourceTrack =
                        trackController_.getAudioTrack(device->sidechain.sourceTrackId);
                    if (sourceTrack) {
                        tePlugin->setSidechainSourceID(sourceTrack->itemID);
                        tePlugin->guessSidechainRouting();
                    }
                } else {
                    tePlugin->setSidechainSourceID({});
                }
            }

            // Both MIDI and Audio sidechain routes use MidiBroadcastBus + MidiReceivePlugin
            // for TE's native LFO resync. Audio sidechain generates synthetic MIDI from
            // AudioSidechainMonitorPlugin; MIDI sidechain uses real MIDI from
            // SidechainMonitorPlugin.
            if (device->sidechain.isActive()) {
                DBG("AudioBridge::devicePropertyChanged - sidechain set (type="
                    << (int)device->sidechain.type
                    << "), ensuring MidiReceive + monitors for source track "
                    << device->sidechain.sourceTrackId);
                pluginManager_.ensureMidiReceive(track.id, device->id,
                                                 device->sidechain.sourceTrackId);
                if (device->sidechain.type == SidechainConfig::Type::MIDI)
                    pluginManager_.checkSidechainMonitor(device->sidechain.sourceTrackId);
                if (device->sidechain.type == SidechainConfig::Type::Audio)
                    pluginManager_.checkAudioSidechainMonitor(device->sidechain.sourceTrackId);
            } else {
                pluginManager_.removeMidiReceive(track.id, device->id);
            }
            // Re-check monitors on current track (may no longer need them)
            pluginManager_.checkSidechainMonitor(track.id);
            pluginManager_.checkAudioSidechainMonitor(track.id);

            return;
        }
    }
}

// =============================================================================
// ClipManagerListener implementation
// =============================================================================

void AudioBridge::clipsChanged() {
    clipSynchronizer_.clipsChanged();
}

void AudioBridge::clipPropertyChanged(ClipId clipId) {
    clipSynchronizer_.clipPropertyChanged(clipId);
}

void AudioBridge::clipSelectionChanged(ClipId clipId) {
    clipSynchronizer_.clipSelectionChanged(clipId);
}

// =============================================================================
// Clip Synchronization (delegated to ClipSynchronizer)
// =============================================================================

void AudioBridge::syncClipToEngine(ClipId clipId) {
    clipSynchronizer_.syncClipToEngine(clipId);
}

void AudioBridge::removeClipFromEngine(ClipId clipId) {
    clipSynchronizer_.removeClipFromEngine(clipId);
}

te::Clip* AudioBridge::getArrangementTeClip(ClipId clipId) const {
    return clipSynchronizer_.getArrangementTeClip(clipId);
}

// =============================================================================
// Session Clip Lifecycle (delegated to ClipSynchronizer)
// =============================================================================

bool AudioBridge::syncSessionClipToSlot(ClipId clipId) {
    return clipSynchronizer_.syncSessionClipToSlot(clipId);
}

void AudioBridge::removeSessionClipFromSlot(ClipId clipId) {
    return clipSynchronizer_.removeSessionClipFromSlot(clipId);
}

void AudioBridge::launchSessionClip(ClipId clipId, bool forceImmediate) {
    return clipSynchronizer_.launchSessionClip(clipId, forceImmediate);
}

void AudioBridge::stopSessionClip(ClipId clipId) {
    clipSynchronizer_.stopSessionClip(clipId);
}

void AudioBridge::stopSessionClipQueued(ClipId clipId, LaunchQuantize quantize) {
    clipSynchronizer_.stopSessionClipQueued(clipId, quantize);
}

te::Clip* AudioBridge::getSessionTeClip(ClipId clipId) {
    return clipSynchronizer_.getSessionTeClip(clipId);
}

// =============================================================================
// Plugin Loading
// =============================================================================

void AudioBridge::captureAllPluginStates() {
    pluginManager_.captureAllPluginStates();
}

void AudioBridge::captureWarpMarkerStates() {
    auto& cm = ClipManager::getInstance();
    for (auto& clip : cm.getArrangementClips()) {
        if (clip.isAudio() && clip.warpEnabled) {
            auto markers = clipSynchronizer_.getWarpMarkers(clip.id);
            DBG("captureWarpMarkerStates: clip " << clip.id
                                                 << " warpEnabled=" << (int)clip.warpEnabled
                                                 << " markers=" << (int)markers.size());
            auto* mutableClip = cm.getClip(clip.id);
            if (mutableClip) {
                mutableClip->warpMarkers.clear();
                for (const auto& m : markers) {
                    mutableClip->warpMarkers.push_back({m.sourceTime, m.warpTime});
                }
                DBG("captureWarpMarkerStates: stored " << mutableClip->warpMarkers.size()
                                                       << " markers into ClipInfo");
            }
        }
    }
}

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

TrackId AudioBridge::getTrackIdForTeTrack(te::EditItemID itemId) const {
    // Reverse lookup: find MAGDA TrackId from TE EditItemID
    TrackId result = INVALID_TRACK_ID;
    trackController_.withTrackMapping([&](const auto& mapping) {
        for (const auto& [trackId, teTrack] : mapping) {
            if (teTrack && teTrack->itemID == itemId) {
                result = trackId;
                break;
            }
        }
    });
    return result;
}

te::Plugin::Ptr AudioBridge::getPlugin(DeviceId deviceId) const {
    return pluginManager_.getPlugin(deviceId);
}

te::AutomatableParameter* AudioBridge::resolveControlTarget(const ControlTarget& target) const {
    switch (target.kind) {
        case ControlTarget::Kind::TrackVolume: {
            auto* track = getAudioTrack(target.devicePath.trackId);
            if (!track)
                return nullptr;
            if (auto* vp = track->getVolumePlugin())
                return vp->volParam.get();
            return nullptr;
        }

        case ControlTarget::Kind::TrackPan: {
            auto* track = getAudioTrack(target.devicePath.trackId);
            if (!track)
                return nullptr;
            if (auto* vp = track->getVolumePlugin())
                return vp->panParam.get();
            return nullptr;
        }

        case ControlTarget::Kind::SendLevel: {
            auto* track = getAudioTrack(target.devicePath.trackId);
            if (!track)
                return nullptr;
            if (auto* auxSend = track->getAuxSendPlugin(target.sendBusIndex))
                return auxSend->gain.get();
            return nullptr;
        }

        case ControlTarget::Kind::PluginParam: {
            DeviceId deviceId = target.devicePath.getDeviceId();
            if (deviceId == INVALID_DEVICE_ID)
                return nullptr;
            auto plugin = getPlugin(deviceId);
            if (!plugin)
                return nullptr;
            auto params = plugin->getAutomatableParameters();
            if (target.paramIndex >= 0 && target.paramIndex < static_cast<int>(params.size()))
                return params[static_cast<size_t>(target.paramIndex)];
            return nullptr;
        }

        case ControlTarget::Kind::DeviceMacro:
            return pluginManager_.findMacroParameterForAutomation(
                target.devicePath.trackId, target.devicePath, target.paramIndex);

        case ControlTarget::Kind::ModParam:
            return pluginManager_.findModifierParameterForAutomation(
                target.devicePath.trackId, target.devicePath, target.modId, target.modParamIndex);
    }
    return nullptr;
}

DeviceProcessor* AudioBridge::getDeviceProcessor(DeviceId deviceId) const {
    return pluginManager_.getDeviceProcessor(deviceId);
}

namespace {
te::ExternalPlugin* asExternalPlugin(te::Plugin::Ptr plugin) {
    return dynamic_cast<te::ExternalPlugin*>(plugin.get());
}
}  // namespace

int AudioBridge::getPluginNumPrograms(DeviceId deviceId) const {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(deviceId))) {
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->getNumPrograms();
    }
    return 0;
}

int AudioBridge::getPluginCurrentProgram(DeviceId deviceId) const {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(deviceId))) {
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->getCurrentProgram();
    }
    return 0;
}

juce::String AudioBridge::getPluginProgramName(DeviceId deviceId, int programIndex) const {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(deviceId))) {
        if (auto* pi = ext->getAudioPluginInstance()) {
            if (programIndex >= 0 && programIndex < pi->getNumPrograms())
                return pi->getProgramName(programIndex);
        }
    }
    return {};
}

bool AudioBridge::setPluginCurrentProgram(DeviceId deviceId, int programIndex) {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(deviceId))) {
        if (auto* pi = ext->getAudioPluginInstance()) {
            if (programIndex >= 0 && programIndex < pi->getNumPrograms()) {
                pi->setCurrentProgram(programIndex);
                return true;
            }
        }
    }
    return false;
}

namespace {

// Loads / saves a .vstpreset blob via JUCE's VST3Client extension. Two-mode
// visitor: when `dataIn` is non-empty we apply it as a preset; otherwise we
// pull the current state into `dataOut`.
struct Vst3PresetVisitor : juce::ExtensionsVisitor {
    juce::MemoryBlock dataIn;
    juce::MemoryBlock dataOut;
    bool ok = false;
    bool save = false;

    void visitVST3Client(const VST3Client& client) override {
        if (save) {
            dataOut = client.getPreset();
            ok = dataOut.getSize() > 0;
        } else {
            ok = client.setPreset(dataIn);
        }
    }
};

}  // namespace

bool AudioBridge::loadPluginPresetFile(DeviceId deviceId, const juce::File& presetFile) {
    if (!presetFile.existsAsFile())
        return false;

    auto* ext = asExternalPlugin(pluginManager_.getPlugin(deviceId));
    if (ext == nullptr)
        return false;
    auto* pi = ext->getAudioPluginInstance();
    if (pi == nullptr)
        return false;

    const auto extension = presetFile.getFileExtension().toLowerCase();
    bool applied = false;

    if (extension == ".vstpreset") {
        juce::MemoryBlock raw;
        if (!presetFile.loadFileAsData(raw))
            return false;
        Vst3PresetVisitor visitor;
        visitor.save = false;
        visitor.dataIn = std::move(raw);
        pi->getExtensions(visitor);
        applied = visitor.ok;
    } else if (extension == ".aupreset") {
        juce::MemoryBlock raw;
        if (!presetFile.loadFileAsData(raw))
            return false;
        // JUCE's AU wrapper reads the plist directly and pushes it to the
        // unit via kAudioUnitProperty_ClassInfo. Note: setStateInformation
        // is NOT what we want — that's JUCE's own state envelope, which
        // wraps the AU class-info plist and would not match a bare .aupreset.
        pi->setCurrentProgramStateInformation(raw.getData(), (int)raw.getSize());
        applied = true;  // AU API doesn't report success; assume ok.
    }

    if (applied) {
        // Persist the new state into TE's ValueTree so project save / undo
        // / param refresh sees it. Mirrors PluginManager::capturePluginState.
        ext->flushPluginStateToValueTree();
    }
    return applied;
}

bool AudioBridge::savePluginPresetFile(DeviceId deviceId, const juce::File& presetFile) {
    auto* ext = asExternalPlugin(pluginManager_.getPlugin(deviceId));
    if (ext == nullptr)
        return false;
    auto* pi = ext->getAudioPluginInstance();
    if (pi == nullptr)
        return false;

    const auto extension = presetFile.getFileExtension().toLowerCase();

    if (extension == ".vstpreset") {
        Vst3PresetVisitor visitor;
        visitor.save = true;
        pi->getExtensions(visitor);
        if (!visitor.ok)
            return false;
        presetFile.getParentDirectory().createDirectory();
        return presetFile.replaceWithData(visitor.dataOut.getData(), visitor.dataOut.getSize());
    }

    if (extension == ".aupreset") {
        juce::MemoryBlock raw;
        pi->getCurrentProgramStateInformation(raw);
        if (raw.getSize() == 0)
            return false;
        presetFile.getParentDirectory().createDirectory();
        return presetFile.replaceWithData(raw.getData(), raw.getSize());
    }

    return false;
}

te::VirtualMidiInputDevice* AudioBridge::getQwertyMidiDevice() {
    if (!qwertyMidiDevice_) {
        // Check if it already exists (persisted from a previous session).
        // Only accept actual VirtualMidiInputDevice instances — a physical
        // device with the same name would break the cast and leave the
        // feature silently disabled.
        for (auto& dev : engine_.getDeviceManager().getMidiInDevices()) {
            if (dev->getName() == "QWERTY Keyboard" &&
                dynamic_cast<te::VirtualMidiInputDevice*>(dev.get())) {
                qwertyMidiDevice_ = dev;
                break;
            }
        }

        // Create if not found
        if (!qwertyMidiDevice_) {
            auto result = engine_.getDeviceManager().createVirtualMidiDevice("QWERTY Keyboard");
            if (result.wasOk()) {
                for (auto& dev : engine_.getDeviceManager().getMidiInDevices()) {
                    if (dev->getName() == "QWERTY Keyboard" &&
                        dynamic_cast<te::VirtualMidiInputDevice*>(dev.get())) {
                        qwertyMidiDevice_ = dev;
                        break;
                    }
                }
                // Freshly created — the live playback context's
                // InputDeviceInstance list is now stale. Flag so the next
                // call after the graph is allocated triggers a refresh.
                // Persisted devices skip this: their instance already exists
                // in the context from when it was first built.
                if (qwertyMidiDevice_)
                    qwertyNeedsContextRefresh_ = true;
            } else {
                DBG("Failed to create QWERTY virtual MIDI device: " << result.getErrorMessage());
            }
        }

        if (qwertyMidiDevice_)
            DBG("QWERTY virtual MIDI device ready");
    }

    // #1054: After creating a virtual MIDI device the live playback context
    // doesn't know about it — setTarget() calls from setTrackMidiInput
    // silently fail to find an InputDeviceInstance and routing breaks on
    // Windows/Linux (macOS happens to refresh implicitly). Force a rebuild
    // once the graph is allocated so the device picks up its instance.
    // Retried on every call until the graph is ready, so users who toggle
    // QWERTY before audio startup aren't permanently broken.
    if (qwertyNeedsContextRefresh_) {
        if (auto* ctx = edit_.getCurrentPlaybackContext(); ctx && ctx->isPlaybackGraphAllocated()) {
            ctx->reallocate();
            qwertyNeedsContextRefresh_ = false;
        }
    }

    return dynamic_cast<te::VirtualMidiInputDevice*>(qwertyMidiDevice_.get());
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

    // Collect MAGDA track IDs for stale-check
    std::unordered_set<TrackId> magdaTrackIds;
    for (const auto& track : tracks) {
        magdaTrackIds.insert(track.id);
    }

    // Remove TE tracks that no longer exist in MAGDA (e.g. after clearAllTracks)
    auto mappedIds = trackController_.getAllTrackIds();
    for (auto trackId : mappedIds) {
        if (magdaTrackIds.find(trackId) == magdaTrackIds.end()) {
            pluginManager_.cleanupTrackPlugins(trackId);
            trackController_.removeAudioTrack(trackId);
        }
    }

    // First pass: create all TE tracks so routing targets exist
    for (const auto& track : tracks) {
        ensureTrackMapping(track.id);
    }

    // Diff-based plugin sync: global orphan cleanup + per-track additive sync
    pluginManager_.syncAllPlugins();

    // Post-sync: routing, volume, and state (needs TE tracks + plugins to exist)
    for (const auto& track : tracks) {
        if (auto* teTrack = getAudioTrack(track.id)) {
            // Sync mute/solo state to TE (essential on project load)
            teTrack->setMute(track.muted);
            teTrack->setSolo(track.soloed);

            // Sync audio output routing (group/aux targets now exist from first pass)
            trackController_.setTrackAudioOutput(track.id, track.audioOutputDevice);

            // Sync volume/pan
            setTrackVolume(track.id, track.volume);
            setTrackPan(track.id, track.pan);

            // Sync frozen state from TE → MAGDA (e.g. on edit load)
            bool teFrozen = teTrack->isFrozen(te::AudioTrack::individualFreeze);
            if (track.frozen != teFrozen) {
                tm.getTrack(track.id)->frozen = teFrozen;
            }
        }
    }

    // Sync master channel volume/pan to Tracktion Engine
    masterChannelChanged();

#if JUCE_DEBUG
    pluginManager_.validateMappingConsistency();
#endif
}

void AudioBridge::syncTrackPlugins(TrackId trackId) {
    pluginManager_.syncTrackPlugins(trackId);

    // Auto-route MIDI for instruments only if this track is selected or armed
    // (Aux tracks never receive MIDI)
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (trackInfo && trackInfo->type != TrackType::Aux) {
        bool hasInstrument = false;
        for (const auto& element : trackInfo->chainElements) {
            if (std::holds_alternative<DeviceInfo>(element)) {
                const auto& device = std::get<DeviceInfo>(element);
                if (device.isInstrument) {
                    hasInstrument = true;
                    break;
                }
            }
        }

        if (hasInstrument) {
            bool isSelected = (trackId == lastSelectedTrack_);
            bool isArmed = trackInfo->recordArmed;
            if (isSelected || isArmed) {
                setTrackMidiInput(trackId, "all");
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
                params[static_cast<size_t>(change.paramIndex)]->setParameterFromHost(
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

    // Detect playback context recreation (e.g. after edit.restartPlayback())
    // and re-establish MIDI routing which is lost when the context is rebuilt
    auto* currentContext = edit_.getCurrentPlaybackContext();
    if (currentContext != lastPlaybackContext_) {
        lastPlaybackContext_ = currentContext;
        if (currentContext != nullptr)
            updateMidiRoutingForSelection();
    }

    // Poll for reversed proxy file completion (delegated to ClipSynchronizer)
    ClipId pendingClipId = clipSynchronizer_.getPendingReverseClipId();
    if (pendingClipId != INVALID_CLIP_ID) {
        const auto& clipIdToEngineId = clipSynchronizer_.getClipIdToEngineId();
        auto it = clipIdToEngineId.find(pendingClipId);
        if (it != clipIdToEngineId.end()) {
            for (auto* track : te::getAudioTracks(edit_)) {
                for (auto* teClip : track->getClips()) {
                    if (teClip->itemID.toString().toStdString() == it->second) {
                        if (auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip)) {
                            auto proxyFile = audioClip->getPlaybackFile().getFile();
                            if (proxyFile.existsAsFile()) {
                                DBG("REVERSE TIMER: proxy ready — reallocating ("
                                    << proxyFile.getFullPathName() << ")");
                                clipSynchronizer_.clearPendingReverseClipId();
                                if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                                    ctx->reallocate();
                                    if (clipSynchronizer_.onGraphReallocated)
                                        clipSynchronizer_.onGraphReallocated();
                                }
                            }
                        }
                        break;
                    }
                }
            }
        } else {
            clipSynchronizer_.clearPendingReverseClipId();
        }
    }

    // NOTE: Window state sync is now handled by PluginWindowManager's timer

    // Automation playback — sample curves at playhead and apply to parameters
    automationPlayback_.process();

    // Automation recording — detect transport transitions, manage recording lifecycle
    automationRecording_.process();

    // Update metering from level measurers (runs at 30 FPS on message thread)
    trackController_.withTrackMapping(
        [this](const std::map<TrackId, te::AudioTrack*>& trackMapping) {
            trackController_.withMeterClients(
                [&](std::map<TrackId, TrackController::MeterClientEntry>& meterClients) {
                    for (const auto& [trackId, track] : trackMapping) {
                        if (!track)
                            continue;

                        // Get the meter client for this track
                        auto clientIt = meterClients.find(trackId);
                        if (clientIt == meterClients.end())
                            continue;

                        auto& client = clientIt->second.client;

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
                        recordingMeteringBuffer_.pushLevels(trackId, data);

                        // Write audio peak to sidechain bus for Audio-triggered modulators
                        float peak = std::max(data.peakL, data.peakR);
                        SidechainTriggerBus::getInstance().setAudioPeakLevel(trackId, peak);
                    }
                });
        });

    // Update per-device metering
    deviceMetering_.updateAllClients();

    // Feed track-level meter data to inner rack devices and rack volume sliders.
    // NOTE: We use the track's output levels for all racks/devices on that track.
    // This means multiple racks on the same track will show identical meters.
    // Per-rack metering would require intercepting audio inside each RackType,
    // which TE doesn't expose without custom graph nodes.
    {
        auto meteringMap = pluginManager_.getRackSyncManager().getMeteringMap();
        for (const auto& [trackId, info] : meteringMap) {
            MeterData trackMeter;
            if (!meteringBuffer_.peekLatest(trackId, trackMeter))
                continue;

            for (auto devId : info.deviceIds) {
                deviceMetering_.ensureEntry(devId);
                deviceMetering_.setDirectLevels(devId, trackMeter.peakL, trackMeter.peakR);
            }

            for (auto rackId : info.rackIds) {
                deviceMetering_.ensureRackEntry(rackId);
                deviceMetering_.setRackDirectLevels(rackId, trackMeter.peakL, trackMeter.peakR);
            }
        }
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
// Automation Recording
// =============================================================================

void AudioBridge::setAutomationWriteEnabled(bool enabled) {
    automationRecording_.setWriteEnabled(enabled);
}

bool AudioBridge::isAutomationWriteEnabled() const {
    return automationRecording_.isWriteEnabled();
}

void AudioBridge::setAutomationMode(AutomationMode mode) {
    automationRecording_.setMode(mode);
}

AutomationMode AudioBridge::getAutomationMode() const {
    return automationRecording_.getMode();
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

juce::BigInteger AudioBridge::getEnabledInputChannels() const {
    juce::BigInteger enabled;
    auto& dm = engine_.getDeviceManager();
    for (auto* dev : dm.getWaveInputDevices()) {
        if (dev->isEnabled()) {
            for (const auto& ch : dev->getChannels())
                enabled.setBit(ch.indexInDevice, true);
        }
    }
    return enabled;
}

std::map<int, juce::String> AudioBridge::getInputDeviceNamesByChannel() const {
    std::map<int, juce::String> result;
    auto& dm = engine_.getDeviceManager();
    for (auto* dev : dm.getWaveInputDevices()) {
        if (dev->isEnabled()) {
            for (const auto& ch : dev->getChannels())
                result[ch.indexInDevice] = dev->getName();
        }
    }
    return result;
}

juce::BigInteger AudioBridge::getEnabledOutputChannels() const {
    juce::BigInteger enabled;
    auto& dm = engine_.getDeviceManager();
    for (auto* dev : dm.getWaveOutputDevices()) {
        if (dev->isEnabled()) {
            for (const auto& ch : dev->getChannels())
                enabled.setBit(ch.indexInDevice, true);
        }
    }
    return enabled;
}

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

    // Build a name->identifier map once so we can pass BOTH identifier and
    // display name into ControllerRegistry's matcher — stored entries may use
    // either form (see magda::midi::matches). TE's MidiInputDevice exposes the
    // display name; the JUCE identifier comes from getAvailableDevices().
    std::unordered_map<juce::String, juce::String> nameToJuceId;
    for (const auto& d : juce::MidiInput::getAvailableDevices())
        nameToJuceId[d.name] = d.identifier;

    // Controller ports are NOT excluded from instrument routing. A typical
    // MIDI keyboard (e.g. Launchkey Mini) exposes a single MIDI port for both
    // note messages AND control-change knobs; excluding the whole port would
    // break note playback. The ControllerRouter intercepts only the specific
    // CC numbers that have bindings; everything else passes through to TE
    // tracks. A future "surface-only" flag can opt specific ports (MCU, etc.)
    // out of track routing when the controller is truly control-only.
    for (auto& midiInput : dm.getMidiInDevices()) {
        if (!midiInput)
            continue;
        if (!midiInput->isEnabled()) {
            midiInput->setEnabled(true);
            DBG("Enabled MIDI input device: " << midiInput->getName());
        }
    }

    DBG("All MIDI input devices enabled in Tracktion Engine");
}

bool AudioBridge::isSurfaceOnlyMidiInput(const juce::String& liveIdentifier,
                                         const juce::String& liveName) const {
    juce::StringArray keys;
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        keys = surfaceOnlyMidiInputPorts_;
    }

    for (const auto& key : keys) {
        if (magda::midi::matches(key, liveIdentifier, liveName))
            return true;
    }

    return false;
}

void AudioBridge::removeSurfaceOnlyMidiInputTargets() {
    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext)
        return;

    bool removedAnyRouting = false;
    auto& tm = TrackManager::getInstance();

    for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
        if (auto* midiDevice = dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
            if (!isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName()))
                continue;

            for (const auto& trackInfo : tm.getTracks()) {
                auto* track = getAudioTrack(trackInfo.id);
                if (!track)
                    continue;

                auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
                if (result)
                    removedAnyRouting = true;
            }
        }
    }

    if (removedAnyRouting && playbackContext->isPlaybackGraphAllocated())
        playbackContext->reallocate();
}

void AudioBridge::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        return;
    }

    auto* playbackContext = edit_.getCurrentPlaybackContext();
    if (!playbackContext) {
        // Store for later when playback context becomes available
        pendingMidiRoutes_.push_back({trackId, midiDeviceId});
        return;
    }

    if (midiDeviceId.isEmpty()) {
        // Disable MIDI input - remove this track as target from all MIDI inputs
        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            // Check if this is a MIDI input device
            if (dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
                [[maybe_unused]] auto result =
                    inputDeviceInstance->removeTarget(track->itemID, nullptr);
            }
        }
    } else if (midiDeviceId == "all") {
        // Route ALL MIDI input devices to this track
        bool addedAnyRouting = false;
        bool removedAnyRouting = false;

        // Determine TE monitor mode from track's inputMonitor setting
        auto teMonitorMode = te::InputDevice::MonitorMode::on;  // default for backward compat
        if (auto* trackInfo = TrackManager::getInstance().getTrack(trackId)) {
            teMonitorMode = toTeMonitorMode(trackInfo->inputMonitor);
        }

        for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
            // Check if this is a MIDI input device
            if (auto* midiDevice =
                    dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
                // Skip TE's virtual "All MIDI Ins" aggregate device — we're already
                // routing each physical device individually, so including it would
                // duplicate every MIDI message.
                if (midiDevice->getName() == "All MIDI Ins")
                    continue;

                // Script-owned DAW/control-surface ports are excluded from
                // track routing. A Launchkey-style device can expose a
                // separate musical MIDI port for notes while its DAW port
                // feeds Lua session controls only.
                if (isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName())) {
                    auto result = inputDeviceInstance->removeTarget(track->itemID, nullptr);
                    if (result) {
                        removedAnyRouting = true;
                    }
                    continue;
                }

                // Make sure the device is enabled
                if (!midiDevice->isEnabled()) {
                    midiDevice->setEnabled(true);
                }

                // Set monitor mode based on track's inputMonitor setting
                midiDevice->setMonitorMode(teMonitorMode);

                // Set this track as target for live MIDI
                auto result =
                    inputDeviceInstance->setTarget(track->itemID, true, nullptr);  // true = MIDI
                if (result.has_value()) {
                    // Enable monitoring but not recording
                    (*result)->recordEnabled = false;
                    addedAnyRouting = true;
                }
            }
        }

        // Reallocate the playback graph to include the new MIDI input nodes
        if (addedAnyRouting || removedAnyRouting) {
            if (playbackContext->isPlaybackGraphAllocated()) {
                playbackContext->reallocate();
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
                        break;
                    }
                }
            }
        }

        if (midiDevice) {
            if (isSurfaceOnlyMidiInput(midiDevice->getDeviceID(), midiDevice->getName())) {
                bool removedAnyRouting = false;
                for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                    if (&inputDeviceInstance->owner == midiDevice) {
                        if (inputDeviceInstance->removeTarget(track->itemID, nullptr)) {
                            removedAnyRouting = true;
                        }
                        break;
                    }
                }
                if (removedAnyRouting && playbackContext->isPlaybackGraphAllocated())
                    playbackContext->reallocate();
                return;
            }

            if (!midiDevice->isEnabled()) {
                midiDevice->setEnabled(true);
            }

            // Set monitor mode based on track's inputMonitor setting
            auto teMonitorModeSpecific = te::InputDevice::MonitorMode::on;  // default
            if (auto* trackInfo2 = TrackManager::getInstance().getTrack(trackId)) {
                teMonitorModeSpecific = toTeMonitorMode(trackInfo2->inputMonitor);
            }
            midiDevice->setMonitorMode(teMonitorModeSpecific);

            // Find the InputDeviceInstance for this MIDI device
            for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                if (&inputDeviceInstance->owner == midiDevice) {
                    auto result = inputDeviceInstance->setTarget(track->itemID, true, nullptr);
                    if (result.has_value()) {
                        (*result)->recordEnabled = false;
                        addedRouting = true;
                    }
                    break;
                }
            }
        }

        // Reallocate the playback graph to include the new MIDI input node
        if (addedRouting) {
            if (playbackContext->isPlaybackGraphAllocated()) {
                playbackContext->reallocate();
            }
        }
    }
}

void AudioBridge::setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName) {
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        surfaceOnlyMidiInputPorts_.clear();
        if (midiDeviceIdOrName.isNotEmpty()) {
            surfaceOnlyMidiInputPorts_.addIfNotAlreadyThere(midiDeviceIdOrName);

            if (auto resolved = magda::midi::resolve(juce::MidiInput::getAvailableDevices(),
                                                     midiDeviceIdOrName)) {
                surfaceOnlyMidiInputPorts_.addIfNotAlreadyThere(resolved->identifier);
                surfaceOnlyMidiInputPorts_.addIfNotAlreadyThere(resolved->name);
            }
        }
    }

    removeSurfaceOnlyMidiInputTargets();
    updateMidiRoutingForSelection();
}

void AudioBridge::clearSurfaceOnlyMidiInputPorts() {
    {
        juce::ScopedLock lock(surfaceOnlyMidiInputLock_);
        surfaceOnlyMidiInputPorts_.clear();
    }

    updateMidiRoutingForSelection();
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

bool AudioBridge::loadSamplerSample(DeviceId deviceId, const juce::File& file) {
    auto plugin = getPlugin(deviceId);
    if (plugin) {
        if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
            sampler->loadSample(file);
            return true;
        }
    }
    return false;
}

// =============================================================================
// Warp Markers (delegated to ClipSynchronizer)
// =============================================================================

void AudioBridge::setTransientSensitivity(ClipId clipId, float sensitivity) {
    clipSynchronizer_.setTransientSensitivity(clipId, sensitivity);
}

bool AudioBridge::getTransientTimes(ClipId clipId) {
    return clipSynchronizer_.getTransientTimes(clipId);
}

void AudioBridge::enableWarp(ClipId clipId) {
    clipSynchronizer_.enableWarp(clipId);
}

void AudioBridge::disableWarp(ClipId clipId) {
    clipSynchronizer_.disableWarp(clipId);
}

std::vector<WarpMarkerInfo> AudioBridge::getWarpMarkers(ClipId clipId) {
    return clipSynchronizer_.getWarpMarkers(clipId);
}

int AudioBridge::addWarpMarker(ClipId clipId, double sourceTime, double warpTime) {
    return clipSynchronizer_.addWarpMarker(clipId, sourceTime, warpTime);
}

double AudioBridge::moveWarpMarker(ClipId clipId, int index, double newWarpTime) {
    return clipSynchronizer_.moveWarpMarker(clipId, index, newWarpTime);
}

void AudioBridge::removeWarpMarker(ClipId clipId, int index) {
    clipSynchronizer_.removeWarpMarker(clipId, index);
}

// =============================================================================
// MIDI Recording Support
// =============================================================================

void AudioBridge::resetSynthsOnTrack(TrackId trackId) {
    auto* audioTrack = trackController_.getAudioTrack(trackId);
    if (!audioTrack)
        return;

    // Reset all synth plugins on the track to prevent stuck notes after recording
    for (auto* plugin : audioTrack->pluginList) {
        if (plugin && plugin->isSynth()) {
            plugin->reset();
        }
    }
}

void AudioBridge::ensureSessionMonitorPlugin() {
    if (sessionMonitorPlugin_)
        return;

    // Use the master plugin list — it always renders regardless of which tracks
    // are active, ensuring the audio monitor processes every buffer.
    auto& masterList = edit_.getMasterPluginList();

    // Check if a SessionMonitorPlugin already exists
    for (int i = 0; i < masterList.size(); ++i) {
        if (auto* existing = dynamic_cast<SessionMonitorPlugin*>(masterList[i])) {
            sessionMonitorPlugin_ = existing;
            sessionMonitorPlugin_->setAudioMonitor(&sessionAudioMonitor_);
            return;
        }
    }

    // Create and insert the plugin at position 0
    auto pluginState = juce::ValueTree(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, SessionMonitorPlugin::xmlTypeName, nullptr);
    masterList.insertPlugin(pluginState, 0);

    // Find the newly created plugin
    for (int i = 0; i < masterList.size(); ++i) {
        if (auto* mon = dynamic_cast<SessionMonitorPlugin*>(masterList[i])) {
            sessionMonitorPlugin_ = mon;
            sessionMonitorPlugin_->setAudioMonitor(&sessionAudioMonitor_);
            return;
        }
    }
}

}  // namespace magda
