#include "AudioBridge.hpp"

#include <iostream>
#include <unordered_set>

#include "../core/ClipOperations.hpp"
#include "../engine/PluginWindowManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"
#include "AudioThumbnailManager.hpp"

namespace magda {

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit)
    : engine_(engine),
      edit_(edit),
      trackController_(engine, edit),
      pluginManager_(engine, edit, trackController_, pluginWindowBridge_, transportState_),
      clipSynchronizer_(edit, trackController_, warpMarkerManager_) {
    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

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
        // Note: ClipManager listener removed by ClipSynchronizer destructor

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
            for (auto& [trackId, track] : trackMapping) {
                if (track) {
                    auto* levelMeter = track->getLevelMeterPlugin();
                    if (levelMeter) {
                        trackController_.removeMeterClient(trackId, levelMeter);
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
    // Track property changed (volume, pan, mute, solo, recordArmed) - sync to Tracktion Engine
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

            // Sync recordArmed state to InputDeviceInstance
            auto* playbackContext = edit_.getCurrentPlaybackContext();
            if (playbackContext) {
                // Find any MIDI input device instances routed to this track
                for (auto* inputDeviceInstance : playbackContext->getAllInputs()) {
                    if (dynamic_cast<te::MidiInputDevice*>(&inputDeviceInstance->owner)) {
                        auto targets = inputDeviceInstance->getTargets();
                        for (auto targetID : targets) {
                            if (targetID == track->itemID) {
                                // Found a MIDI input routed to this track - sync record armed state
                                inputDeviceInstance->setRecordingEnabled(track->itemID,
                                                                         trackInfo->recordArmed);
                                DBG("Synced recordArmed=" << (int)trackInfo->recordArmed
                                                          << " to MIDI input '"
                                                          << inputDeviceInstance->owner.getName()
                                                          << "' for track " << trackId);
                                break;
                            }
                        }
                    }
                }
            }
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

void AudioBridge::launchSessionClip(ClipId clipId) {
    return clipSynchronizer_.launchSessionClip(clipId);
}

void AudioBridge::stopSessionClip(ClipId clipId) {
    clipSynchronizer_.stopSessionClip(clipId);
}

te::Clip* AudioBridge::getSessionTeClip(ClipId clipId) {
    return clipSynchronizer_.getSessionTeClip(clipId);
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
                                if (auto* ctx = edit_.getCurrentPlaybackContext())
                                    ctx->reallocate();
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

    // Update metering from level measurers (runs at 30 FPS on message thread)
    // Use trackController's thread-safe accessors
    trackController_.withTrackMapping([this](const auto& trackMapping) {
        trackController_.withMeterClients([&](const auto& meterClients) {
            // Update track metering
            for (const auto& [trackId, track] : trackMapping) {
                if (!track)
                    continue;

                // Get the meter client for this track
                auto clientIt = meterClients.find(trackId);
                if (clientIt == meterClients.end())
                    continue;

                // Note: getAndClearAudioLevel() mutates the client, but we're accessing
                // through const reference. This is safe because the mutation is internal
                // to the client's thread-safe implementation.
                auto& client = const_cast<te::LevelMeasurer::Client&>(clientIt->second);

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

// =============================================================================
// Warp Markers (delegated to ClipSynchronizer)
// =============================================================================

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

}  // namespace magda
