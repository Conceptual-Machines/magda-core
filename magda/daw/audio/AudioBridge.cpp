#include "AudioBridge.hpp"

#include <iostream>

namespace magda {

AudioBridge::AudioBridge(te::Engine& engine, te::Edit& edit) : engine_(engine), edit_(edit) {
    // Register as TrackManager listener
    TrackManager::getInstance().addListener(this);

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

        // Unregister all meter clients
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
    // Track property changed (mute, solo, etc.) - may need to sync
    auto* track = getAudioTrack(trackId);
    if (track) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        if (trackInfo) {
            track->setMute(trackInfo->muted);
            track->setSolo(trackInfo->soloed);
        }
    }
}

void AudioBridge::trackDevicesChanged(TrackId trackId) {
    // Devices on a track changed - resync that track's plugins
    syncTrackPlugins(trackId);
}

void AudioBridge::devicePropertyChanged(DeviceId deviceId) {
    // A device property changed (gain, level, etc.) - sync to processor
    auto* processor = getDeviceProcessor(deviceId);
    if (!processor)
        return;

    // Find the DeviceInfo to get updated values
    // We need to search through all tracks to find this device
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        for (const auto& element : track.chainElements) {
            if (std::holds_alternative<DeviceInfo>(element)) {
                const auto& device = std::get<DeviceInfo>(element);
                if (device.id == deviceId) {
                    // Sync processor from the updated DeviceInfo
                    processor->syncFromDeviceInfo(device);
                    return;
                }
            }
        }
    }
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

        juce::ScopedLock lock(mappingLock_);
        trackMapping_[trackId] = track;

        // Don't register meter client yet - will do it when LevelMeter is added
        std::cout << "Created Tracktion AudioTrack for MAGDA track " << trackId << ": " << name
                  << std::endl;
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

void AudioBridge::updateMetering() {
    // This would be called from the audio thread
    // For now, we use the timer callback for metering
}

void AudioBridge::timerCallback() {
    // Update metering from level measurers (runs at 30 FPS on message thread)
    juce::ScopedLock lock(mappingLock_);

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

        // Convert from dB to linear gain (0-1)
        data.peakL = juce::Decibels::decibelsToGain(levelL.dB);
        data.peakR = juce::Decibels::decibelsToGain(levelR.dB);

        // Check for clipping
        data.clipped = data.peakL > 1.0f || data.peakR > 1.0f;

        // RMS would require accumulation over time - simplified for now
        data.rmsL = data.peakL * 0.7f;  // Rough approximation
        data.rmsR = data.peakR * 0.7f;

        meteringBuffer_.pushLevels(trackId, data);
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
            // Sync initial state from DeviceInfo
            processor->syncFromDeviceInfo(device);
            deviceProcessors_[device.id] = std::move(processor);
        }

        // Apply device state
        plugin->setEnabled(!device.bypassed);
        std::cout << "Loaded device " << device.id << " (" << device.name << ") as plugin"
                  << std::endl;
    }

    return plugin;
}

}  // namespace magda
