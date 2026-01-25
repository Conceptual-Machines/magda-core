#include "AudioBridge.hpp"

#include <iostream>

namespace magda {

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit) : engine_(engine), edit_(edit) {
    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

    // Master metering will be registered when playback context is available
    // (done in timerCallback when context exists)
    masterMeterRegistered_ = false;

    // Start timer for metering updates (30 FPS for smooth UI)
    startTimerHz(30);

    std::cout << "AudioBridge initialized" << std::endl;
}

AudioBridge::~AudioBridge() {
    stopTimer();
    TrackManager::getInstance().removeListener(this);

    // Remove all meter clients before clearing mappings
    {
        juce::ScopedLock lock(mappingLock_);

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

void AudioBridge::devicePropertyChanged(DeviceId deviceId) {
    // A device property changed (gain, level, etc.) - sync to processor
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
// Plugin Loading
// =============================================================================

te::Plugin::Ptr AudioBridge::loadBuiltInPlugin(TrackId trackId, const juce::String& type) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        // Create track if it doesn't exist
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        juce::String name = trackInfo ? trackInfo->name : "Track";
        track = createAudioTrack(trackId, name);
    }

    if (!track)
        return nullptr;

    te::Plugin::Ptr plugin;

    if (type.equalsIgnoreCase("tone") || type.equalsIgnoreCase("tonegenerator")) {
        plugin = createToneGenerator(track);
    } else if (type.equalsIgnoreCase("volume") || type.equalsIgnoreCase("volumeandpan")) {
        plugin = createVolumeAndPan(track);
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

    if (plugin) {
        std::cout << "Loaded built-in plugin: " << type << " on track " << trackId << std::endl;
    } else {
        std::cerr << "Failed to load built-in plugin: " << type << std::endl;
    }

    return plugin;
}

te::Plugin::Ptr AudioBridge::loadExternalPlugin(TrackId trackId,
                                                const juce::PluginDescription& description) {
    auto* track = getAudioTrack(trackId);
    if (!track) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        juce::String name = trackInfo ? trackInfo->name : "Track";
        track = createAudioTrack(trackId, name);
    }

    if (!track)
        return nullptr;

    // Create external plugin using the description
    auto plugin =
        edit_.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, description);

    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);
        std::cout << "Loaded external plugin: " << description.name << " on track " << trackId
                  << std::endl;
    } else {
        std::cerr << "Failed to load external plugin: " << description.name << std::endl;
    }

    return plugin;
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
            std::cout << "Removing existing LevelMeter at position " << i << std::endl;

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

            std::cout << "Registered meter client for track " << trackId
                      << " with LevelMeter at end of plugin chain" << std::endl;
        }
    }

    return plugin;
}

// =============================================================================
// Track Mapping
// =============================================================================

te::AudioTrack* AudioBridge::getAudioTrack(TrackId trackId) {
    juce::ScopedLock lock(mappingLock_);
    auto it = trackMapping_.find(trackId);
    return it != trackMapping_.end() ? it->second : nullptr;
}

te::Plugin::Ptr AudioBridge::getPlugin(DeviceId deviceId) {
    juce::ScopedLock lock(mappingLock_);
    auto it = deviceToPlugin_.find(deviceId);
    return it != deviceToPlugin_.end() ? it->second : nullptr;
}

DeviceProcessor* AudioBridge::getDeviceProcessor(DeviceId deviceId) {
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

    std::cout << "AudioBridge: syncing " << tracks.size() << " tracks" << std::endl;

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
            auto it = deviceToPlugin_.find(deviceId);
            if (it != deviceToPlugin_.end()) {
                auto plugin = it->second;
                pluginToDevice_.erase(plugin.get());
                deviceToPlugin_.erase(it);
                plugin->deleteFromParent();
            }
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

    // Ensure LevelMeter is at the end of the plugin chain for metering
    addLevelMeterToTrack(trackId);

    // Debug: Print the final plugin chain for track 1
    if (trackId == 1 && teTrack) {
        std::cout << "\nTrack 1 final plugin chain:" << std::endl;
        auto& plugins = teTrack->pluginList;
        for (int i = 0; i < plugins.size(); ++i) {
            auto* p = plugins[i];
            std::cout << "  [" << i << "] " << p->getName() << " (enabled: " << p->isEnabled()
                      << ")";

            if (auto* tone = dynamic_cast<te::ToneGeneratorPlugin*>(p)) {
                std::cout << " - freq=" << tone->frequency << ", level=" << tone->level;
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }
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
    ParameterChange change;
    while (parameterQueue_.pop(change)) {
        auto plugin = getPlugin(change.deviceId);
        if (plugin) {
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

void AudioBridge::timerCallback() {
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
    }
    return plugin;
}

te::Plugin::Ptr AudioBridge::createVolumeAndPan(te::AudioTrack* track) {
    if (!track)
        return nullptr;

    // VolumeAndPanPlugin has create() that returns ValueTree
    auto plugin = edit_.getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::create());
    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);
    }
    return plugin;
}

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
        } else if (device.pluginId.containsIgnoreCase("volume")) {
            plugin = createVolumeAndPan(track);
            if (plugin) {
                processor = std::make_unique<VolumeProcessor>(device.id, plugin);
            }
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
        // External plugin - need to find matching description
        // This will be fully implemented in Phase 2
        std::cout << "External plugin loading deferred to Phase 2: " << device.name << std::endl;
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
    if (!track) {
        DBG("AudioBridge::setTrackVolume - track not found: " << trackId);
        return;
    }

    // Find VolumeAndPanPlugin on track
    if (auto volPan = track->pluginList.findFirstPluginOfType<te::VolumeAndPanPlugin>()) {
        float db = volume > 0.0f ? juce::Decibels::gainToDecibels(volume) : -100.0f;
        volPan->setVolumeDb(db);
    }
}

float AudioBridge::getTrackVolume(TrackId trackId) const {
    auto* track = const_cast<AudioBridge*>(this)->getAudioTrack(trackId);
    if (!track) {
        return 1.0f;
    }

    if (auto volPan = track->pluginList.findFirstPluginOfType<te::VolumeAndPanPlugin>()) {
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

    if (auto volPan = track->pluginList.findFirstPluginOfType<te::VolumeAndPanPlugin>()) {
        volPan->setPan(pan);
    }
}

float AudioBridge::getTrackPan(TrackId trackId) const {
    auto* track = const_cast<AudioBridge*>(this)->getAudioTrack(trackId);
    if (!track) {
        return 0.0f;
    }

    if (auto volPan = track->pluginList.findFirstPluginOfType<te::VolumeAndPanPlugin>()) {
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
                inputDeviceInstance->removeTarget(track->itemID, nullptr);
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
    auto* track = const_cast<AudioBridge*>(this)->getAudioTrack(trackId);
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
    auto* track = const_cast<AudioBridge*>(this)->getAudioTrack(trackId);
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

}  // namespace magda
