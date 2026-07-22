#include "AudioBridge.hpp"

#include <algorithm>
#include <unordered_set>

#include "../core/AutomationManager.hpp"
#include "../core/ChainRoutingModel.hpp"
#include "../core/ClipOperations.hpp"
#include "../core/Config.hpp"
#include "../core/InternalDeviceKind.hpp"
#include "../core/ModulatorEngine.hpp"
#include "../core/RackInfo.hpp"
#include "../engine/PluginWindowManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"
#include "AudioThumbnailManager.hpp"
#include "Vst3Preset.hpp"
#include "modifiers/ADSRDebugLog.hpp"
#include "plugins/MidiInThruSync.hpp"
#include "session/SessionMonitorPlugin.hpp"

namespace magda {

namespace {

bool inputHasTarget(te::InputDeviceInstance& input, te::EditItemID targetID) {
    for (auto existingTargetID : input.getTargets()) {
        if (existingTargetID == targetID)
            return true;
    }
    return false;
}

te::WaveInputDevice* getLiveWaveInputDevice(te::Engine& engine,
                                            te::InputDeviceInstance* inputDeviceInstance) {
    if (!inputDeviceInstance)
        return nullptr;

    auto* owner = &inputDeviceInstance->owner;
    for (auto* waveInput : engine.getDeviceManager().getWaveInputDevices()) {
        if (waveInput == owner)
            return waveInput;
    }

    return nullptr;
}

// Measurers whose owning device currently exists: hardware wave input devices
// (owned by the engine's DeviceManager) and track wave input devices (owned by
// their source AudioTrack). Only measurers in this set may be dereferenced when
// unregistering LevelMeasurer clients — anything else may be a stale pointer to
// a destroyed device.
std::unordered_set<te::LevelMeasurer*> collectAliveInputMeasurers(
    te::Engine& engine, const std::map<TrackId, te::AudioTrack*>& trackMapping) {
    std::unordered_set<te::LevelMeasurer*> alive;

    for (auto* waveInput : engine.getDeviceManager().getWaveInputDevices())
        if (waveInput != nullptr)
            alive.insert(&waveInput->levelMeasurer);

    for (const auto& [trackId, track] : trackMapping) {
        juce::ignoreUnused(trackId);
        if (track != nullptr)
            alive.insert(&track->getWaveInputDevice().levelMeasurer);
    }

    return alive;
}

te::WaveInputDevice* getLiveTrackWaveInputDevice(
    const std::map<TrackId, te::AudioTrack*>& trackMapping,
    te::InputDeviceInstance* inputDeviceInstance) {
    if (!inputDeviceInstance)
        return nullptr;

    auto* owner = &inputDeviceInstance->owner;
    for (const auto& [trackId, track] : trackMapping) {
        juce::ignoreUnused(trackId);
        if (!track)
            continue;
        auto* waveInput = &track->getWaveInputDevice();
        if (waveInput == owner)
            return waveInput;
    }

    return nullptr;
}

MeterData readMeterClient(te::LevelMeasurer::Client& client) {
    MeterData data;

    auto levelL = client.getAndClearAudioLevel(0);
    auto levelR = client.getAndClearAudioLevel(1);

    data.peakL = juce::Decibels::decibelsToGain(levelL.dB);
    data.peakR = juce::Decibels::decibelsToGain(levelR.dB);
    data.clipped = data.peakL > 1.0f || data.peakR > 1.0f;
    data.rmsL = data.peakL * 0.7f;
    data.rmsR = data.peakR * 0.7f;

    return data;
}

void mergeMeterData(MeterData& dest, const MeterData& src) {
    dest.peakL = std::max(dest.peakL, src.peakL);
    dest.peakR = std::max(dest.peakR, src.peakR);
    dest.rmsL = std::max(dest.rmsL, src.rmsL);
    dest.rmsR = std::max(dest.rmsR, src.rmsR);
    dest.clipped = dest.clipped || src.clipped;
}

}  // namespace

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit)
    : engine_(engine),
      edit_(edit),
      trackController_(engine, edit),
      pluginManager_(engine, edit, trackController_, pluginWindowBridge_, transportState_),
      mixer_(edit, trackController_),
      midiInputRouter_(engine, edit, trackController_),
      controlTargetResolver_(trackController_, pluginManager_),
      sidechainRouting_(pluginManager_, trackController_),
      insertDeviceEnablement_(edit),
      samplerFileLoader_(pluginManager_),
      clipSynchronizer_(edit, trackController_, warpMarkerManager_),
      automationPlayback_(*this, edit),
      automationRecording_(edit) {
    // Wire up async plugin load completion callback to notify UI
    pluginManager_.onAsyncPluginLoaded = [](TrackId trackId) {
        TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
    };

    // Re-establish MIDI routing and input monitor state after graph reallocate
    clipSynchronizer_.onGraphReallocated = [this]() {
        updateMidiInputRouting();
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

    MAGDA_ADSR_AUDIO_LOG("AudioBridge initialized");
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
        if (masterMeterContext_ != nullptr) {
            if (auto* ctx = edit_.getCurrentPlaybackContext()) {
                ctx->masterLevels.removeClient(masterMeterClient_);
            }
            masterMeterContext_ = nullptr;
        }

        // Unregister all track meter clients (via trackController)
        trackController_.withTrackMapping([this](const auto& trackMapping) {
            for (auto& [trackId, track] : trackMapping) {
                trackController_.removeMeterClient(trackId);
            }
        });

        // Unregister live input meter clients from any still-alive device
        // measurers BEFORE destroying the Client objects. Hardware wave input
        // devices are engine-owned and outlive this bridge; destroying a
        // registered Client leaves a dangling pointer in the device's
        // LevelMeasurer that the audio thread writes through.
        trackController_.withTrackMapping(
            [this](const std::map<TrackId, te::AudioTrack*>& trackMapping) {
                const auto aliveMeasurers = collectAliveInputMeasurers(engine_, trackMapping);
                for (auto& [trackId, entry] : inputMeterClients_) {
                    juce::ignoreUnused(trackId);
                    if (entry.measurer && aliveMeasurers.count(entry.measurer) != 0)
                        entry.measurer->removeClient(entry.client);
                }
            });
        inputMeterClients_.clear();

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

void AudioBridge::resetTestState() {
    juce::ScopedLock lock(mappingLock_);

    DeviceMeteringManager::unregisterForEdit(edit_);
    deviceMetering_.clear();
    DeviceMeteringManager::registerForEdit(edit_, &deviceMetering_);

    trackController_.withTrackMapping([this](const auto& trackMapping) {
        for (auto& [trackId, track] : trackMapping) {
            juce::ignoreUnused(track);
            trackController_.removeMeterClient(trackId);
        }
    });

    // Same invariant as the destructor: unregister from still-alive device
    // measurers before destroying the Client objects (hardware wave input
    // devices persist across tests on the shared engine).
    trackController_.withTrackMapping(
        [this](const std::map<TrackId, te::AudioTrack*>& trackMapping) {
            const auto aliveMeasurers = collectAliveInputMeasurers(engine_, trackMapping);
            for (auto& [trackId, entry] : inputMeterClients_) {
                juce::ignoreUnused(trackId);
                if (entry.measurer && aliveMeasurers.count(entry.measurer) != 0)
                    entry.measurer->removeClient(entry.client);
            }
        });
    inputMeterClients_.clear();

    automationPlayback_.clearAllLanes();
    trackController_.clearAllMappings();
    pluginManager_.clearAllMappings();
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

    // Post-load sidechain monitor sync: DeviceInfo::sidechain is persisted,
    // but the source-track monitor plugins are MAGDA-injected infrastructure
    // that is not part of the saved model. They are normally inserted by
    // deviceModifiersChanged / handleDeviceSidechainChanged, neither of which
    // fires on a plain load, so a restored MIDI sidechain would stay silent
    // until the user re-picks the source. Cheap and idempotent.
    sidechainRouting_.refreshAllSourceMonitors();

    // Post-load external-insert port enablement (#1623): a saved send/return
    // on a disabled hardware port resolves to noDevice until the port is
    // enabled. Cheap and idempotent.
    refreshInsertDeviceEnablement();
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

            resyncAllInputMonitors();

            // Update MIDI routing when record arm changes
            // (armed tracks should receive MIDI even when not selected)
            updateMidiInputRouting();

            // The external-instrument sendback guard is arm-gated: arming
            // re-admits the synth's own ports so its keyboard records,
            // disarming drops them again (#1623). Re-apply BEFORE the arm
            // sync so the re-added targets exist when they get armed.
            midiInputRouter_.reapplyExternalInstrumentSendbackGuard(trackId);

            syncRecordArmedToTE(trackId);
        }
    }

    // Forward to automation recording engine
    automationRecording_.onTrackPropertyChanged(trackId);
}

void AudioBridge::trackSelectionChanged(TrackId newTrackId) {
    juce::ignoreUnused(newTrackId);
    updateMidiInputRouting();
}

void AudioBridge::updateMidiInputRouting() {
    midiInputRouter_.updateMidiInputRouting();
}

void AudioBridge::refreshInsertDeviceEnablement() {
    if (insertDeviceEnablement_.refresh())
        if (auto* ctx = edit_.getCurrentPlaybackContext();
            ctx != nullptr && ctx->isPlaybackGraphAllocated())
            ctx->reallocate();
}

void AudioBridge::resyncAllInputMonitors() {
    // Coalesced: a burst of monitor changes in one message-loop drain applies
    // once, and never re-entrantly from trackPropertyChanged / graph
    // reallocation. A real mode change restarts all transports, so applying
    // per-notification would stack restarts against the running graph.
    midiInputRouter_.requestInputMonitorResync();
}

void AudioBridge::trackDevicesChanged(TrackId trackId) {
    DBG("AudioBridge::trackDevicesChanged: trackId=" << trackId);
    // Devices on a track changed - resync that track's plugins
    syncTrackPlugins(trackId);

    // An added/removed external insert claims/releases its hardware ports
    // (#1623). Cheap and idempotent when no inserts changed.
    refreshInsertDeviceEnablement();
}

void AudioBridge::deviceAdded(const ChainNodePath& devicePath, const DeviceInfo& device) {
    // Single place the "open the editor when a device is added" preference lives.
    // The device declares whether it even has a floating window (external plugins
    // do; internal MAGDA devices render inline), so this stays a pure policy check
    // with no type-sniffing. trackDevicesChanged already ran and created the
    // engine plugin; defer the open so the chain UI rebuild settles first.
    if (!device.hasEditorWindow())
        return;
    if (!Config::getInstance().getOpenPluginWindowOnDrop())
        return;

    std::weak_ptr<int> alive = lifetimeToken_;
    juce::MessageManager::callAsync([this, devicePath, alive]() {
        if (alive.expired() || isShuttingDown_.load(std::memory_order_acquire))
            return;
        showPluginWindow(devicePath);
    });
}

void AudioBridge::deviceModifiersChanged(TrackId trackId) {
    MAGDA_ADSR_AUDIO_LOG("deviceModifiersChanged trackId=" << trackId);

    // Skip the modifier resync when this notify is the playback engine
    // echoing a baked curve value (e.g. LFO rate) back into MAGDA state.
    // TE already drove the modifier param on the audio thread; resyncing
    // would just push the same value back through and fight the curve.
    if (AutomationManager::getInstance().isApplyingAutomationWrite())
        return;

    // Modifier properties changed (rate, waveform, sync, trigger mode) - resync only modifiers
    MAGDA_ADSR_AUDIO_LOG("follower-bridge resync-start trackId=" << trackId);
    pluginManager_.resyncDeviceModifiers(trackId);
    MAGDA_ADSR_AUDIO_LOG("follower-bridge resync-done trackId=" << trackId);

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
    sidechainRouting_.refreshAllSourceMonitors();
    MAGDA_ADSR_AUDIO_LOG("follower-bridge monitor-refresh-done trackId=" << trackId);

    // Re-check MIDI routing in case trigger mode changed to/from MIDI
    updateMidiInputRouting();
    MAGDA_ADSR_AUDIO_LOG("follower-bridge midi-refresh-done trackId=" << trackId);
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
    // Skip automation playback echoes. TE already drove the MacroParameter
    // from its baked curve, and forwarding the echo into the recorder makes
    // write mode record its own playback instead of just the user's macro move.
    if (AutomationManager::getInstance().isApplyingAutomationWrite())
        return;

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

void AudioBridge::deviceParameterChanged(const ChainNodePath& devicePath, int paramIndex,
                                         float newValue) {
    // A single device parameter changed - sync only that parameter to processor
    auto* processor = getDeviceProcessor(devicePath);
    if (!processor) {
        return;
    }

    processor->setParameterByIndex(paramIndex, ParameterModelValue{newValue});

    // Forward to automation recording engine. Pass the full path: under
    // section-local IDs the bare DeviceId is ambiguous (FX and post-FX devices
    // share ids), so the recorder must record against the exact section.
    automationRecording_.onDeviceParameterChanged(devicePath, paramIndex, newValue);
}

void AudioBridge::devicePropertyChanged(const ChainNodePath& devicePath) {
    const auto deviceId = devicePath.getDeviceId();
    // A device property changed (gain, bypass, etc.) - sync to processor
    auto* processor = getDeviceProcessor(devicePath);

    auto* device = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    if (!device)
        return;

    // Engine enablement = the device's own bypass flag gated by the track's
    // chain power (insert-chain devices only).
    const bool effectiveEnabled =
        TrackManager::getInstance().isDeviceEffectivelyEnabled(devicePath, *device);

    if (processor) {
        processor->syncFromDeviceInfo(*device);
    }
    // Apply enablement directly on the TE plugin: syncFromDeviceInfo only
    // knows the device's own bypassed flag, not the chain gate; and plugins
    // without a processor (e.g. Chord Engine) have no other sync path.
    if (auto tePlugin = pluginManager_.getPlugin(devicePath)) {
        tePlugin->setEnabled(effectiveEnabled);
        tePlugin->setDeltaSoloEnabled(device->deltaSolo);
        daw::audio::syncPluginMidiInThru(tePlugin.get(), device->midiInThru);
    }

    // Wrapped instruments consume MIDI while active. Only top-level devices own
    // instrument wrapper racks; post-fx/mixer-analysis ids are section-local and
    // can overlap with a top-level instrument id.
    if (devicePath.getType() == ChainNodeType::TopLevelDevice) {
        auto& rackManager = pluginManager_.getInstrumentRackManager();
        if (auto* rackInstance = rackManager.getRackInstance(deviceId)) {
            rackInstance->setEnabled(effectiveEnabled);
        }
        // Keep the wrapper's raw-MIDI passthrough in sync with the routing model
        // (always on for a plain instrument; midiInThru-controlled for a
        // MIDI-output device), so notes keep reaching downstream devices.
        rackManager.setMidiInThru(deviceId, routing::makeRoutingNode(*device).passesRawMidiInput());
    }

    // Push gain to the audio-graph atomic so DeviceGainNode picks it up.
    // DeviceGainNode sits OUTSIDE the plugin in the TE graph (between the plugin and
    // the level meter), so it stays active even when the plugin is bypassed. Force
    // unity while bypassed so the slider stops attenuating signal that isn't going
    // through the plugin (#1189). The user's gainValue is preserved on DeviceInfo
    // and gets re-pushed when the device is re-enabled.
    deviceMetering_.setGain(devicePath, effectiveEnabled ? device->gainValue : 1.0f);

    // When bypass changes, resync modifiers so they are removed/restored
    pluginManager_.resyncDeviceModifiers(devicePath.trackId);

    sidechainRouting_.handleDeviceSidechainChanged(devicePath.trackId, *device);

    // External-insert routing changed (#1623): auto-enable any hardware port
    // the insert now references, and re-apply the MIDI feedback guard so the
    // send target's own input port is dropped from this track's routing.
    if (classifyInternalDevice(device->pluginId) == InternalDeviceKind::ExternalInsert) {
        refreshInsertDeviceEnablement();
        midiInputRouter_.reapplyExternalInstrumentSendbackGuard(devicePath.trackId);
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

te::Plugin::Ptr AudioBridge::getPlugin(const ChainNodePath& devicePath) const {
    return pluginManager_.getPlugin(devicePath);
}

AudioBridge::ResolvedDevice AudioBridge::resolveDevice(const ChainNodePath& devicePath) const {
    ResolvedDevice out;
    out.info = TrackManager::getInstance().getDeviceInChainByPath(devicePath);
    out.plugin = getPlugin(devicePath);
    return out;
}

te::AutomatableParameter* AudioBridge::resolveControlTarget(const ControlTarget& target) const {
    return controlTargetResolver_.resolve(target);
}

DeviceProcessor* AudioBridge::getDeviceProcessor(const ChainNodePath& devicePath) const {
    return pluginManager_.getDeviceProcessor(devicePath);
}

namespace {
te::ExternalPlugin* asExternalPlugin(te::Plugin::Ptr plugin) {
    return dynamic_cast<te::ExternalPlugin*>(plugin.get());
}

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

juce::String AudioBridge::getVst3DeviceId(const ChainNodePath& devicePath) const {
    auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath));
    if (ext == nullptr)
        return {};
    auto* pi = ext->getAudioPluginInstance();
    if (pi == nullptr)
        return {};
    // Pull the current state as a .vstpreset; its header carries the 32-char
    // class id. Visitor stays empty for non-VST3 plugins.
    Vst3PresetVisitor visitor;
    visitor.save = true;
    pi->getExtensions(visitor);
    if (!visitor.ok)
        return {};  // not a VST3 plugin
    return vst3::classIdFromPreset(visitor.dataOut);
}

int AudioBridge::getPluginNumPrograms(const ChainNodePath& devicePath) const {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath))) {
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->getNumPrograms();
    }
    return 0;
}
int AudioBridge::getPluginCurrentProgram(const ChainNodePath& devicePath) const {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath))) {
        if (auto* pi = ext->getAudioPluginInstance())
            return pi->getCurrentProgram();
    }
    return 0;
}
juce::String AudioBridge::getPluginProgramName(const ChainNodePath& devicePath,
                                               int programIndex) const {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath))) {
        if (auto* pi = ext->getAudioPluginInstance()) {
            if (programIndex >= 0 && programIndex < pi->getNumPrograms())
                return pi->getProgramName(programIndex);
        }
    }
    return {};
}
bool AudioBridge::setPluginCurrentProgram(const ChainNodePath& devicePath, int programIndex) {
    if (auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath))) {
        if (auto* pi = ext->getAudioPluginInstance()) {
            if (programIndex >= 0 && programIndex < pi->getNumPrograms()) {
                pi->setCurrentProgram(programIndex);
                return true;
            }
        }
    }
    return false;
}
bool AudioBridge::loadPluginPresetFile(const ChainNodePath& devicePath,
                                       const juce::File& presetFile) {
    if (!presetFile.existsAsFile())
        return false;

    auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath));
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
        pi->setCurrentProgramStateInformation(raw.getData(), (int)raw.getSize());
        applied = true;
    }

    if (applied)
        ext->flushPluginStateToValueTree();
    return applied;
}
bool AudioBridge::savePluginPresetFile(const ChainNodePath& devicePath,
                                       const juce::File& presetFile) {
    auto* ext = asExternalPlugin(pluginManager_.getPlugin(devicePath));
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
    return midiInputRouter_.getQwertyMidiDevice();
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

bool AudioBridge::pushParameterChange(const ChainNodePath& devicePath, int paramIndex,
                                      float value) {
    // Delegate to ParameterManager
    return parameterManager_.pushChange(devicePath, paramIndex, value);
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

            // Restore input routing (source tracks now exist from first pass).
            // Guarded so the frequent tracksChanged-driven syncAll stays idempotent.
            // "track:" restores are skipped when the source track has no live TE
            // track (e.g. mid-teardown) — re-applying them would recreate an
            // input-device instance that immediately goes stale.
            auto hasLiveSourceTrack = [this](const juce::String& value) {
                if (!value.startsWith("track:"))
                    return true;
                TrackId sourceId =
                    value.fromFirstOccurrenceOf("track:", false, false).getIntValue();
                return trackController_.getAudioTrack(sourceId) != nullptr;
            };
            if (track.audioInputDevice.isNotEmpty() && hasLiveSourceTrack(track.audioInputDevice) &&
                trackController_.getTrackAudioInput(track.id) != track.audioInputDevice)
                trackController_.setTrackAudioInput(track.id, track.audioInputDevice);
            if (track.midiInputDevice.startsWith("track:") &&
                hasLiveSourceTrack(track.midiInputDevice) &&
                midiInputRouter_.getTrackMidiInput(track.id) != track.midiInputDevice)
                midiInputRouter_.setTrackMidiInput(track.id, track.midiInputDevice);

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
    updateMidiInputRouting();
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
        auto plugin = getPlugin(change.devicePath);
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

void AudioBridge::refreshInputMeterClients(const std::map<TrackId, te::AudioTrack*>& trackMapping) {
    std::map<TrackId, te::LevelMeasurer*> desired;

    if (auto* playbackContext = edit_.getCurrentPlaybackContext()) {
        for (const auto& [trackId, track] : trackMapping) {
            if (!track)
                continue;

            auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
            if (!trackInfo || trackInfo->audioInputDevice.isEmpty())
                continue;

            for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                auto* inputOwner = getLiveWaveInputDevice(engine_, inputDeviceInstance);
                if (!inputOwner)
                    inputOwner = getLiveTrackWaveInputDevice(trackMapping, inputDeviceInstance);
                if (!inputOwner)
                    continue;

                if (!inputHasTarget(*inputDeviceInstance, track->itemID))
                    continue;

                if (!inputDeviceInstance->isLivePlayEnabled(*track))
                    continue;

                desired[trackId] = &inputOwner->levelMeasurer;
                break;
            }
        }
    }

    // A stale measurer pointer may only be dereferenced (for removeClient) if its
    // owning device still exists, so build the alive set from the devices
    // themselves: hardware wave inputs live in the DeviceManager, track-as-input
    // devices are owned by their source AudioTrack. Deriving this set from
    // `desired` (as this code used to) is wrong: a measurer that is alive but no
    // longer desired — e.g. its consumer's Monitor was switched Off, flipping
    // isLivePlayEnabled() — must still get removeClient() before its Client is
    // destroyed. Skipping that leaves a dangling Client* inside
    // WaveInputDevice::levelMeasurer, which the audio graph then writes through
    // on every block (heap corruption / audio-thread livelock, #1690).
    const auto aliveMeasurers = collectAliveInputMeasurers(engine_, trackMapping);

    for (auto& [trackId, measurer] : desired) {
        if (!measurer)
            continue;

        auto [it, inserted] = inputMeterClients_.try_emplace(trackId);
        auto& entry = it->second;

        if (inserted) {
            entry.measurer = measurer;
            measurer->addClient(entry.client);
        } else if (entry.measurer != measurer) {
            if (entry.measurer && aliveMeasurers.count(entry.measurer) != 0)
                entry.measurer->removeClient(entry.client);
            entry.measurer = measurer;
            measurer->addClient(entry.client);
        }
    }

    for (auto it = inputMeterClients_.begin(); it != inputMeterClients_.end();) {
        if (desired.find(it->first) != desired.end()) {
            ++it;
            continue;
        }

        if (it->second.measurer && aliveMeasurers.count(it->second.measurer) != 0)
            it->second.measurer->removeClient(it->second.client);
        it = inputMeterClients_.erase(it);
    }
}

void AudioBridge::onMidiDevicesAvailable() {
    midiInputRouter_.onMidiDevicesAvailable();
}

void AudioBridge::applyPendingMidiRoutes() {
    midiInputRouter_.applyPendingRoutes();
}

void AudioBridge::timerCallback() {
    // Skip all operations if shutting down
    if (isShuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    // Pause all live-engine tick work while an offline render is active (export /
    // mix analysis). The render needs the play context to stay inactive (TE
    // asserts on it); letting the MIDI/context tick, reverse-proxy polling or
    // metering run here can re-touch the context mid-render. restoreAfterRendering()
    // reallocates and resumes once the render completes.
    if (pluginManager_.isRenderingActive())
        return;

    midiInputRouter_.handlePlaybackContextTick();

    // Poll for reversed proxy file completion (delegated to ClipSynchronizer)
    ClipId pendingClipId = clipSynchronizer_.getPendingReverseClipId();
    if (pendingClipId != INVALID_CLIP_ID) {
        auto engineId = clipSynchronizer_.getArrangementEngineId(pendingClipId);
        if (engineId) {
            for (auto* track : te::getAudioTracks(edit_)) {
                for (auto* teClip : track->getClips()) {
                    if (teClip->itemID.toString().toStdString() == *engineId) {
                        if (auto* audioClip = dynamic_cast<te::WaveAudioClip*>(teClip)) {
                            auto playbackFile = audioClip->getPlaybackFile();
                            const bool renderInProgress =
                                engine_.getRenderManager().isProxyBeingGenerated(playbackFile);
                            if (playbackFile.isValid() && !renderInProgress) {
                                DBG("REVERSE TIMER: proxy ready — reallocating ("
                                    << playbackFile.getFile().getFullPathName() << ")");
                                clipSynchronizer_.clearPendingReverseClipId();
                                // AudioFile::isValid() can become true before ReverseRenderJob has
                                // finished validating and releasing the file from Tracktion's audio
                                // cache. Rebuilding during that window preserves the silent reader
                                // created while the proxy did not exist. Once the render manager no
                                // longer owns a job for this proxy, validation is complete and a
                                // new graph can safely open it.
                                if (auto* context = edit_.getCurrentPlaybackContext())
                                    context->reallocate();
                                else
                                    edit_.restartPlayback();
                                if (edit_.getCurrentPlaybackContext() != nullptr) {
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

    // Update metering from level measurers (runs at 30 FPS on message thread).
    // (Skipped entirely during an offline render by the early return above, so
    // the live meters don't twitch to the render's audio either.)
    trackController_.withTrackMapping(
        [this](const std::map<TrackId, te::AudioTrack*>& trackMapping) {
            refreshInputMeterClients(trackMapping);

            trackController_.withMeterClients(
                [&](std::map<TrackId, TrackController::MeterClientEntry>& meterClients) {
                    for (const auto& [trackId, track] : trackMapping) {
                        if (!track)
                            continue;

                        MeterData data;
                        bool hasData = false;

                        // Track output meter from the TE graph tap.
                        auto clientIt = meterClients.find(trackId);
                        if (clientIt != meterClients.end()) {
                            data = readMeterClient(clientIt->second.client);
                            hasData = true;
                        }

                        // Live-monitored input can bypass the track's LevelMeterPlugin tap while
                        // still feeding the master, so merge the input device meter as well.
                        auto inputClientIt = inputMeterClients_.find(trackId);
                        if (inputClientIt != inputMeterClients_.end()) {
                            auto inputData = readMeterClient(inputClientIt->second.client);
                            mergeMeterData(data, inputData);
                            hasData = true;
                        }

                        if (!hasData)
                            continue;

                        meteringBuffer_.pushLevels(trackId, data);
                        recordingMeteringBuffer_.pushLevels(trackId, data);

                        // Write audio peak to sidechain bus for Audio-triggered modulators
                        float peak = std::max(data.peakL, data.peakR);
                        sidechainRouting_.publishAudioPeak(trackId, peak);
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

            for (const auto& devicePath : info.devicePaths) {
                deviceMetering_.ensureEntry(devicePath);
                deviceMetering_.setDirectLevels(devicePath, trackMeter.peakL, trackMeter.peakR);
            }

            for (auto rackId : info.rackIds) {
                deviceMetering_.ensureRackEntry(rackId);
                deviceMetering_.setRackDirectLevels(rackId, trackMeter.peakL, trackMeter.peakR);
            }
        }
    }

    // Keep the master meter client registered on the CURRENT playback context.
    // The context is destroyed + rebuilt after an offline render frees it, so
    // re-register whenever the pointer changes -- otherwise the master VU stays
    // dead after a render (the client was on the old, freed context).
    auto* meterCtx = edit_.getCurrentPlaybackContext();
    if (meterCtx != masterMeterContext_) {
        if (meterCtx != nullptr)
            meterCtx->masterLevels.addClient(masterMeterClient_);
        masterMeterContext_ = meterCtx;
    }

    // Update master metering from playback context's masterLevels
    if (masterMeterContext_ != nullptr) {
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
    mixer_.setTrackVolume(trackId, volume);
}

float AudioBridge::getTrackVolume(TrackId trackId) const {
    return mixer_.getTrackVolume(trackId);
}

void AudioBridge::setTrackPan(TrackId trackId, float pan) {
    mixer_.setTrackPan(trackId, pan);
}

float AudioBridge::getTrackPan(TrackId trackId) const {
    return mixer_.getTrackPan(trackId);
}

void AudioBridge::setMasterVolume(float volume) {
    mixer_.setMasterVolume(volume);
}

float AudioBridge::getMasterVolume() const {
    return mixer_.getMasterVolume();
}

void AudioBridge::setMasterPan(float pan) {
    mixer_.setMasterPan(pan);
}

float AudioBridge::getMasterPan() const {
    return mixer_.getMasterPan();
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

bool AudioBridge::setSessionSlotAudioRecordingTarget(TrackId trackId, int sceneIndex,
                                                     bool enabled) {
    return trackController_.setSessionSlotAudioRecordingTarget(trackId, sceneIndex, enabled);
}

// =============================================================================
// MIDI Routing (for live instrument playback)
// =============================================================================

void AudioBridge::enableAllMidiInputDevices() {
    midiInputRouter_.enableAllMidiInputDevices();
}

void AudioBridge::setTrackMidiInput(TrackId trackId, const juce::String& midiDeviceId) {
    midiInputRouter_.setTrackMidiInput(trackId, midiDeviceId);
}

void AudioBridge::setSurfaceOnlyMidiInputPort(const juce::String& midiDeviceIdOrName) {
    midiInputRouter_.setSurfaceOnlyMidiInputPort(midiDeviceIdOrName);
}

void AudioBridge::clearSurfaceOnlyMidiInputPorts() {
    midiInputRouter_.clearSurfaceOnlyMidiInputPorts();
}

juce::String AudioBridge::getTrackMidiInput(TrackId trackId) const {
    return midiInputRouter_.getTrackMidiInput(trackId);
}

bool AudioBridge::setSessionSlotMidiRecordingTarget(TrackId trackId, int sceneIndex, bool enabled) {
    return midiInputRouter_.setSessionSlotMidiRecordingTarget(trackId, sceneIndex, enabled);
}

// =============================================================================
// Plugin Editor Windows (delegates to PluginWindowManager)
// =============================================================================

void AudioBridge::showPluginWindow(const ChainNodePath& devicePath) {
    auto plugin = getPlugin(devicePath);
    if (plugin)
        pluginWindowBridge_.showPluginWindow(devicePath.getDeviceId(), plugin);
}

void AudioBridge::hidePluginWindow(const ChainNodePath& devicePath) {
    auto plugin = getPlugin(devicePath);
    if (plugin)
        pluginWindowBridge_.hidePluginWindow(devicePath.getDeviceId(), plugin);
}

bool AudioBridge::isPluginWindowOpen(const ChainNodePath& devicePath) const {
    auto plugin = getPlugin(devicePath);
    return plugin ? pluginWindowBridge_.isPluginWindowOpen(plugin) : false;
}

bool AudioBridge::togglePluginWindow(const ChainNodePath& devicePath) {
    auto plugin = getPlugin(devicePath);
    return plugin ? pluginWindowBridge_.togglePluginWindow(devicePath.getDeviceId(), plugin)
                  : false;
}

bool AudioBridge::loadSamplerSample(const ChainNodePath& devicePath, const juce::File& file) {
    return samplerFileLoader_.loadSample(devicePath, file);
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
