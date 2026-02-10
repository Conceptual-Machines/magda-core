#include "PluginManager.hpp"

#include <iostream>
#include <vector>

#include "../core/RackInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"
#include "ModifierHelpers.hpp"
#include "PluginWindowBridge.hpp"
#include "SidechainMonitorPlugin.hpp"
#include "TrackController.hpp"
#include "TransportStateManager.hpp"

namespace magda {

PluginManager::PluginManager(te::Engine& engine, te::Edit& edit, TrackController& trackController,
                             PluginWindowBridge& pluginWindowBridge,
                             TransportStateManager& transportState)
    : engine_(engine),
      edit_(edit),
      trackController_(trackController),
      pluginWindowBridge_(pluginWindowBridge),
      transportState_(transportState),
      instrumentRackManager_(edit),
      rackSyncManager_(edit, *this) {}

// =============================================================================
// Plugin/Device Lookup
// =============================================================================

te::Plugin::Ptr PluginManager::getPlugin(DeviceId deviceId) const {
    juce::ScopedLock lock(pluginLock_);
    auto it = deviceToPlugin_.find(deviceId);
    if (it != deviceToPlugin_.end())
        return it->second;

    // Fall through to rack sync manager for plugins inside racks
    auto* innerPlugin = rackSyncManager_.getInnerPlugin(deviceId);
    if (innerPlugin)
        return innerPlugin;

    return nullptr;
}

DeviceProcessor* PluginManager::getDeviceProcessor(DeviceId deviceId) const {
    juce::ScopedLock lock(pluginLock_);
    auto it = deviceProcessors_.find(deviceId);
    return it != deviceProcessors_.end() ? it->second.get() : nullptr;
}

// =============================================================================
// Plugin Synchronization
// =============================================================================

void PluginManager::syncTrackPlugins(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (!teTrack) {
        teTrack = trackController_.createAudioTrack(trackId, trackInfo->name);
    }

    if (!teTrack)
        return;

    // Get current MAGDA devices and racks from chain elements
    std::vector<DeviceId> magdaDevices;
    std::vector<RackId> magdaRacks;
    for (const auto& element : trackInfo->chainElements) {
        if (isDevice(element)) {
            magdaDevices.push_back(getDevice(element).id);
        } else if (isRack(element)) {
            magdaRacks.push_back(getRack(element).id);
        }
    }

    // Remove TE plugins that no longer exist in MAGDA
    // Collect plugins to remove under lock, then delete outside lock to avoid blocking
    std::vector<DeviceId> toRemove;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [deviceId, plugin] : deviceToPlugin_) {
            // Check if this plugin belongs to this track.
            // For regular plugins: check ownerTrack directly
            // For wrapped instruments: the inner plugin lives inside a rack,
            // so we check if the wrapper rack instance is on this track
            bool belongsToTrack = false;
            auto* owner = plugin->getOwnerTrack();

            if (owner == teTrack) {
                belongsToTrack = true;
            } else if (instrumentRackManager_.getInnerPlugin(deviceId) == plugin.get()) {
                // This is a wrapped instrument — check if we created it for this track
                // by scanning the track's plugin list for our rack instance
                for (int i = 0; i < teTrack->pluginList.size(); ++i) {
                    if (instrumentRackManager_.isWrapperRack(teTrack->pluginList[i])) {
                        if (instrumentRackManager_.getDeviceIdForRack(teTrack->pluginList[i]) ==
                            deviceId) {
                            belongsToTrack = true;
                            break;
                        }
                    }
                }
            }

            if (belongsToTrack) {
                // Check if device still exists in MAGDA
                bool found = std::find(magdaDevices.begin(), magdaDevices.end(), deviceId) !=
                             magdaDevices.end();
                if (!found) {
                    toRemove.push_back(deviceId);
                    pluginsToDelete.push_back(plugin);
                }
            }
        }

        // Remove from mappings while under lock
        for (auto deviceId : toRemove) {
            auto it = deviceToPlugin_.find(deviceId);
            if (it != deviceToPlugin_.end()) {
                pluginToDevice_.erase(it->second.get());
                deviceToPlugin_.erase(it);
            }
            deviceProcessors_.erase(deviceId);
        }
    }

    // Delete plugins outside lock to avoid blocking other threads
    for (size_t i = 0; i < toRemove.size(); ++i) {
        pluginWindowBridge_.closeWindowsForDevice(toRemove[i]);

        // If this was a wrapped instrument, unwrap it (removes rack + rack type)
        if (instrumentRackManager_.getInnerPlugin(toRemove[i]) != nullptr) {
            instrumentRackManager_.unwrap(toRemove[i]);
        } else if (pluginsToDelete[i]) {
            pluginsToDelete[i]->deleteFromParent();
        }
    }

    // Remove stale racks (racks no longer in MAGDA chain elements)
    {
        std::vector<RackId> racksToRemove;
        for (const auto& [rackId, plugin] : deviceToPlugin_) {
            if (rackSyncManager_.isRackInstance(plugin.get())) {
                auto actualRackId = rackSyncManager_.getRackIdForInstance(plugin.get());
                if (std::find(magdaRacks.begin(), magdaRacks.end(), actualRackId) ==
                    magdaRacks.end()) {
                    racksToRemove.push_back(actualRackId);
                }
            }
        }
        // Also check racks not in deviceToPlugin_ (direct iteration of synced racks)
        // This handles racks that were synced but whose RackInstance might have been tracked
        // differently
        for (auto rackId : racksToRemove) {
            rackSyncManager_.removeRack(rackId);
        }
    }

    // Add new plugins for MAGDA devices that don't have TE counterparts
    for (const auto& element : trackInfo->chainElements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);

            juce::ScopedLock lock(pluginLock_);
            if (deviceToPlugin_.find(device.id) == deviceToPlugin_.end()) {
                // Load this device as a plugin
                auto plugin = loadDeviceAsPlugin(trackId, device);
                if (plugin) {
                    deviceToPlugin_[device.id] = plugin;
                    pluginToDevice_[plugin.get()] = device.id;
                }
            }
        } else if (isRack(element)) {
            const auto& rackInfo = getRack(element);

            // Sync rack (creates or updates TE RackType + RackInstance)
            auto rackInstance = rackSyncManager_.syncRack(trackId, rackInfo);
            if (rackInstance) {
                // Check if this rack instance is already on the track
                bool alreadyOnTrack = false;
                for (int i = 0; i < teTrack->pluginList.size(); ++i) {
                    if (teTrack->pluginList[i] == rackInstance.get()) {
                        alreadyOnTrack = true;
                        break;
                    }
                }

                if (!alreadyOnTrack) {
                    teTrack->pluginList.insertPlugin(rackInstance, -1, nullptr);
                }

                // Register inner plugins in our device-to-plugin maps for parameter access
                for (const auto& chain : rackInfo.chains) {
                    for (const auto& chainElement : chain.elements) {
                        if (isDevice(chainElement)) {
                            const auto& device = getDevice(chainElement);
                            auto* innerPlugin = rackSyncManager_.getInnerPlugin(device.id);
                            if (innerPlugin) {
                                juce::ScopedLock lock(pluginLock_);
                                deviceToPlugin_[device.id] = innerPlugin;
                                pluginToDevice_[innerPlugin] = device.id;
                            }
                        }
                    }
                }
            }
        }
    }

    // Aux track: ensure AuxReturnPlugin exists with correct bus number
    if (trackInfo->type == TrackType::Aux && trackInfo->auxBusIndex >= 0) {
        bool hasReturn = false;
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            if (dynamic_cast<te::AuxReturnPlugin*>(teTrack->pluginList[i])) {
                hasReturn = true;
                break;
            }
        }
        if (!hasReturn) {
            auto ret = edit_.getPluginCache().createNewPlugin(te::AuxReturnPlugin::xmlTypeName, {});
            if (ret) {
                if (auto* auxRet = dynamic_cast<te::AuxReturnPlugin*>(ret.get())) {
                    auxRet->busNumber = trackInfo->auxBusIndex;
                }
                teTrack->pluginList.insertPlugin(ret, 0, nullptr);
            }
        }
    }

    // Sync sends: ensure AuxSendPlugins match TrackInfo::sends
    {
        // Collect existing AuxSendPlugin bus numbers
        std::vector<int> existingSendBuses;
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(teTrack->pluginList[i])) {
                existingSendBuses.push_back(auxSend->getBusNumber());
            }
        }

        // Collect desired bus numbers from TrackInfo
        std::vector<int> desiredBuses;
        for (const auto& send : trackInfo->sends) {
            desiredBuses.push_back(send.busIndex);
        }

        // Remove AuxSendPlugins that are no longer needed
        for (int i = teTrack->pluginList.size() - 1; i >= 0; --i) {
            if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(teTrack->pluginList[i])) {
                int bus = auxSend->getBusNumber();
                if (std::find(desiredBuses.begin(), desiredBuses.end(), bus) ==
                    desiredBuses.end()) {
                    auxSend->deleteFromParent();
                }
            }
        }

        // Add missing AuxSendPlugins
        for (const auto& send : trackInfo->sends) {
            bool exists = std::find(existingSendBuses.begin(), existingSendBuses.end(),
                                    send.busIndex) != existingSendBuses.end();
            if (!exists) {
                auto sendPlugin =
                    edit_.getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {});
                if (sendPlugin) {
                    if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(sendPlugin.get())) {
                        auxSend->busNumber = send.busIndex;
                        auxSend->setGainDb(juce::Decibels::gainToDecibels(send.level));
                    }
                    teTrack->pluginList.insertPlugin(sendPlugin, -1, nullptr);
                }
            }
        }

        // Update send levels for existing sends
        for (const auto& send : trackInfo->sends) {
            for (int i = 0; i < teTrack->pluginList.size(); ++i) {
                if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(teTrack->pluginList[i])) {
                    if (auxSend->getBusNumber() == send.busIndex) {
                        auxSend->setGainDb(juce::Decibels::gainToDecibels(send.level));
                        break;
                    }
                }
            }
        }
    }

    // Sync device-level modifiers (LFOs, etc. assigned to plugin parameters)
    syncDeviceModifiers(trackId, teTrack);

    // Sync device-level macros (macro knobs assigned to plugin parameters)
    syncDeviceMacros(trackId, teTrack);

    // Sync sidechain routing for plugins that support it
    syncSidechains(trackId, teTrack);

    // Sidechain monitor: insert on tracks that need audio-thread MIDI detection
    if (trackNeedsSidechainMonitor(trackId))
        ensureSidechainMonitor(trackId);
    else
        removeSidechainMonitor(trackId);

    // Ensure VolumeAndPan is near the end of the chain (before LevelMeter)
    // This is the track's fader control - it should come AFTER audio sources
    ensureVolumePluginPosition(teTrack);

    // Ensure LevelMeter is at the end of the plugin chain for metering
    addLevelMeterToTrack(trackId);

    // Rebuild sidechain LFO cache so audio/MIDI threads see current state
    rebuildSidechainLFOCache();
}

// =============================================================================
// Plugin Loading
// =============================================================================

te::Plugin::Ptr PluginManager::loadBuiltInPlugin(TrackId trackId, const juce::String& type) {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track) {
        // Create track if it doesn't exist
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        juce::String name = trackInfo ? trackInfo->name : "Track";
        track = trackController_.createAudioTrack(trackId, name);
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

PluginLoadResult PluginManager::loadExternalPlugin(TrackId trackId,
                                                   const juce::PluginDescription& description) {
    MAGDA_MONITOR_SCOPE("PluginLoad");

    auto* track = trackController_.getAudioTrack(trackId);
    if (!track) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        juce::String name = trackInfo ? trackInfo->name : "Track";
        track = trackController_.createAudioTrack(trackId, name);
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

te::Plugin::Ptr PluginManager::addLevelMeterToTrack(TrackId trackId) {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track) {
        std::cerr << "Cannot add LevelMeter: track " << trackId << " not found" << std::endl;
        return nullptr;
    }

    // Remove any existing LevelMeter plugins first to avoid duplicates
    auto& plugins = track->pluginList;
    for (int i = plugins.size() - 1; i >= 0; --i) {
        if (auto* levelMeter = dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
            // Unregister meter client from the old LevelMeter (thread-safe)
            trackController_.removeMeterClient(trackId, levelMeter);
            levelMeter->deleteFromParent();
        }
    }

    // Now add a fresh LevelMeter at the end
    auto plugin = loadBuiltInPlugin(trackId, "levelmeter");

    // Register meter client with the new LevelMeter (thread-safe)
    if (plugin) {
        if (auto* levelMeter = dynamic_cast<te::LevelMeterPlugin*>(plugin.get())) {
            trackController_.addMeterClient(trackId, levelMeter);
        }
    }

    return plugin;
}

void PluginManager::ensureVolumePluginPosition(te::AudioTrack* track) const {
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
        // No VolumeAndPanPlugin exists - this is expected for tracks without the volume plugin
        // (Tracktion Engine creates it automatically when needed)
        return;
    }

    // Find LevelMeterPlugin position (if it exists)
    int meterIndex = -1;
    for (int i = 0; i < plugins.size(); ++i) {
        if (dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
            meterIndex = i;
            break;
        }
    }

    // Determine target position: before LevelMeter, or at end if no meter
    int targetIndex = meterIndex >= 0 ? meterIndex : plugins.size();

    // Move VolumeAndPanPlugin to target position if needed
    if (volPanIndex != targetIndex && volPanIndex >= 0) {
        // Tracktion Engine PluginList doesn't have removeObjectWithoutDeleting
        // Instead, we can just reorder by removing and re-adding
        // First remove the plugin (this doesn't delete it, just removes from list)
        volPanPlugin->removeFromParent();

        // After removal, if volume was before meter, meter index shifts down by 1
        int insertIndex = -1;  // Default: append to end
        if (meterIndex >= 0) {
            // If volume was before meter, meter shifted down
            insertIndex = (volPanIndex < meterIndex) ? (meterIndex - 1) : meterIndex;
        }

        // Reinsert at corrected position
        plugins.insertPlugin(volPanPlugin, insertIndex, nullptr);

        DBG("Moved VolumeAndPanPlugin from position "
            << volPanIndex << " to " << (insertIndex >= 0 ? insertIndex : plugins.size() - 1));
    }
}

// =============================================================================
// Device-Level Modifier Sync
// =============================================================================

void PluginManager::updateDeviceModifierProperties(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    DBG("updateDeviceModifierProperties: trackId=" << trackId);

    // Update properties on existing TE modifiers without removing/recreating them
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);
        auto it = deviceModifiers_.find(device.id);
        if (it == deviceModifiers_.end())
            continue;

        auto& existingMods = it->second;
        size_t modIdx = 0;

        for (const auto& modInfo : device.mods) {
            if (!modInfo.enabled || modInfo.links.empty())
                continue;

            if (modIdx >= existingMods.size())
                break;

            auto& modifier = existingMods[modIdx];
            if (!modifier) {
                ++modIdx;
                continue;
            }

            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                applyLFOProperties(lfo, modInfo);
                // TE LFO in note mode needs triggerNoteOn() to start oscillating
                if (modInfo.running && modInfo.triggerMode != LFOTriggerMode::Free)
                    lfo->triggerNoteOn();
            }

            // Update assignment values (mod depth) for each link
            for (const auto& link : modInfo.links) {
                if (!link.isValid())
                    continue;

                te::Plugin::Ptr linkTarget;
                if (link.target.deviceId == device.id) {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = deviceToPlugin_.find(device.id);
                    if (pit != deviceToPlugin_.end())
                        linkTarget = pit->second;
                    if (!linkTarget && device.isInstrument)
                        if (auto* inner = instrumentRackManager_.getInnerPlugin(device.id))
                            linkTarget = inner;
                } else {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = deviceToPlugin_.find(link.target.deviceId);
                    if (pit != deviceToPlugin_.end())
                        linkTarget = pit->second;
                }
                if (!linkTarget)
                    continue;

                auto params = linkTarget->getAutomatableParameters();
                if (link.target.paramIndex >= 0 &&
                    link.target.paramIndex < static_cast<int>(params.size())) {
                    auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                    if (param) {
                        for (auto* assignment : param->getAssignments()) {
                            if (assignment->isForModifierSource(*modifier)) {
                                // Gate triggered LFOs: 0 when not running
                                float effectiveAmount = link.amount;
                                if (modInfo.triggerMode != LFOTriggerMode::Free && !modInfo.running)
                                    effectiveAmount = 0.0f;
                                assignment->value = effectiveAmount;
                                break;
                            }
                        }
                    }
                }
            }

            ++modIdx;
        }
    }
}

void PluginManager::syncDeviceModifiers(TrackId trackId, te::AudioTrack* teTrack) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    // Collect all top-level devices (not inside MAGDA racks) that have active mod links
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);

        // Check if any mod has active links
        bool hasActiveMods = false;
        for (const auto& mod : device.mods) {
            if (mod.enabled && !mod.links.empty()) {
                hasActiveMods = true;
                break;
            }
        }

        // Choose the right ModifierList scope for parameter assignment.
        // Instruments are wrapped in an InstrumentRack — modifiers must live on
        // the rack's ModifierList to reach the inner plugin's parameters.
        // Standalone plugins live directly on the track, so use the track's list.
        // MIDI retrigger is handled programmatically via LFOModifier::triggerNoteOn()
        // rather than relying on MIDI flowing through applyToBuffer().
        te::ModifierList* modList = nullptr;
        if (device.isInstrument) {
            auto rackType = instrumentRackManager_.getRackType(device.id);
            if (rackType)
                modList = &rackType->getModifierList();
        }
        if (!modList)
            modList = teTrack->getModifierList();

        // Remove existing TE modifiers for this device before recreating
        auto& existingMods = deviceModifiers_[device.id];
        if (!existingMods.empty()) {
            // Find target plugin to clean up modifier assignments from its parameters
            te::Plugin::Ptr targetPlugin;
            {
                juce::ScopedLock lock(pluginLock_);
                auto it = deviceToPlugin_.find(device.id);
                if (it != deviceToPlugin_.end())
                    targetPlugin = it->second;
            }
            if (!targetPlugin && device.isInstrument)
                if (auto* inner = instrumentRackManager_.getInnerPlugin(device.id))
                    targetPlugin = inner;

            for (auto& mod : existingMods) {
                if (!mod)
                    continue;

                // Remove modifier assignments from all target parameters
                if (targetPlugin) {
                    for (auto* param : targetPlugin->getAutomatableParameters())
                        param->removeModifier(*mod);
                }

                // Remove the modifier from the ModifierList
                if (modList)
                    modList->state.removeChild(mod->state, nullptr);
            }
        }
        existingMods.clear();

        if (!hasActiveMods || !modList)
            continue;

        // Find the TE plugin for this device
        te::Plugin::Ptr targetPlugin;
        {
            juce::ScopedLock lock(pluginLock_);
            auto it = deviceToPlugin_.find(device.id);
            if (it != deviceToPlugin_.end())
                targetPlugin = it->second;
        }

        // For instruments, the inner plugin inside the rack is what we need
        if (!targetPlugin && device.isInstrument) {
            auto* inner = instrumentRackManager_.getInnerPlugin(device.id);
            if (inner)
                targetPlugin = inner;
        }

        if (!targetPlugin)
            continue;

        // Create TE modifiers for each active mod
        for (const auto& modInfo : device.mods) {
            if (!modInfo.enabled || modInfo.links.empty())
                continue;

            te::Modifier::Ptr modifier;

            switch (modInfo.type) {
                case ModType::LFO: {
                    juce::ValueTree lfoState(te::IDs::LFO);
                    auto lfoMod = modList->insertModifier(lfoState, -1, nullptr);
                    if (!lfoMod)
                        break;

                    if (auto* lfo = dynamic_cast<te::LFOModifier*>(lfoMod.get())) {
                        applyLFOProperties(lfo, modInfo);
                    }
                    modifier = lfoMod;
                    break;
                }

                case ModType::Random: {
                    juce::ValueTree randomState(te::IDs::RANDOM);
                    modifier = modList->insertModifier(randomState, -1, nullptr);
                    break;
                }

                case ModType::Follower: {
                    juce::ValueTree envState(te::IDs::ENVELOPEFOLLOWER);
                    modifier = modList->insertModifier(envState, -1, nullptr);
                    break;
                }

                case ModType::Envelope:
                    break;
            }

            if (!modifier)
                continue;

            existingMods.push_back(modifier);

            // Create modifier assignments for each link
            for (const auto& link : modInfo.links) {
                if (!link.isValid())
                    continue;

                // Device-level mods target parameters on the same device
                // (link.target.deviceId should match device.id)
                te::Plugin::Ptr linkTarget = targetPlugin;
                if (link.target.deviceId != device.id) {
                    // Cross-device link — look up the other device
                    juce::ScopedLock lock(pluginLock_);
                    auto it = deviceToPlugin_.find(link.target.deviceId);
                    if (it != deviceToPlugin_.end())
                        linkTarget = it->second;
                    else
                        continue;
                }

                auto params = linkTarget->getAutomatableParameters();
                if (link.target.paramIndex >= 0 &&
                    link.target.paramIndex < static_cast<int>(params.size())) {
                    auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                    if (param) {
                        // Gate triggered LFOs: start with 0 until triggered
                        float initialAmount = link.amount;
                        if (modInfo.triggerMode != LFOTriggerMode::Free && !modInfo.running)
                            initialAmount = 0.0f;
                        param->addModifier(*modifier, initialAmount);
                        DBG("syncDeviceModifiers: linked mod to '"
                            << param->getParameterName() << "' amount=" << juce::String(link.amount)
                            << " modType=" << (int)modInfo.type
                            << " numAssignments=" << (int)param->getAssignments().size());
                    }
                }
            }
        }
    }
}

// =============================================================================
void PluginManager::triggerLFONoteOn(TrackId trackId) {
    // Trigger resync on all TE LFO modifiers associated with devices on this track
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);
        auto it = deviceModifiers_.find(device.id);
        if (it == deviceModifiers_.end())
            continue;

        for (auto& mod : it->second) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                lfo->triggerNoteOn();
            }
        }
    }

    // Also trigger LFOs inside MAGDA racks on this track
    rackSyncManager_.triggerLFONoteOn(trackId);
}

// =============================================================================
void PluginManager::triggerSidechainNoteOn(TrackId sourceTrackId) {
    if (sourceTrackId < 0 || sourceTrackId >= kMaxCacheTracks)
        return;

    const juce::SpinLock::ScopedLockType lock(cacheLock_);
    auto& entry = sidechainLFOCache_[static_cast<size_t>(sourceTrackId)];
    for (int i = 0; i < entry.count; ++i)
        entry.lfos[static_cast<size_t>(i)]->triggerNoteOn();
}

void PluginManager::rebuildSidechainLFOCache() {
    auto& tm = TrackManager::getInstance();

    // Build new cache on the stack, then swap under lock
    std::array<PerTrackEntry, kMaxCacheTracks> newCache{};

    for (const auto& track : tm.getTracks()) {
        if (track.id < 0 || track.id >= kMaxCacheTracks)
            continue;

        auto& entry = newCache[static_cast<size_t>(track.id)];
        std::vector<te::LFOModifier*> lfos;

        // 1. Self-track LFOs: collect from deviceModifiers_ for this track's devices
        for (const auto& element : track.chainElements) {
            if (!isDevice(element))
                continue;
            const auto& device = getDevice(element);
            auto it = deviceModifiers_.find(device.id);
            if (it == deviceModifiers_.end())
                continue;
            for (auto& mod : it->second) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get()))
                    lfos.push_back(lfo);
            }
        }

        // Also collect from racks on this track
        rackSyncManager_.collectLFOModifiers(track.id, lfos);

        // 2. Cross-track LFOs: for each OTHER track that has a device sidechained
        //    from this track, collect that destination track's LFO modifiers
        for (const auto& otherTrack : tm.getTracks()) {
            if (otherTrack.id == track.id)
                continue;
            bool isDestination = false;
            for (const auto& element : otherTrack.chainElements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    if ((device.sidechain.type == SidechainConfig::Type::MIDI ||
                         device.sidechain.type == SidechainConfig::Type::Audio) &&
                        device.sidechain.sourceTrackId == track.id) {
                        isDestination = true;
                        break;
                    }
                }
            }
            if (!isDestination)
                continue;

            // Collect all LFO modifiers from the destination track
            for (const auto& element : otherTrack.chainElements) {
                if (!isDevice(element))
                    continue;
                const auto& device = getDevice(element);
                auto it = deviceModifiers_.find(device.id);
                if (it == deviceModifiers_.end())
                    continue;
                for (auto& mod : it->second) {
                    if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get()))
                        lfos.push_back(lfo);
                }
            }
            rackSyncManager_.collectLFOModifiers(otherTrack.id, lfos);
        }

        // Write to cache entry (capped at kMaxLFOs)
        entry.count = std::min(static_cast<int>(lfos.size()), PerTrackEntry::kMaxLFOs);
        for (int i = 0; i < entry.count; ++i)
            entry.lfos[static_cast<size_t>(i)] = lfos[static_cast<size_t>(i)];
    }

    // Swap under lock
    {
        const juce::SpinLock::ScopedLockType lock(cacheLock_);
        sidechainLFOCache_ = newCache;
    }
}

// =============================================================================
void PluginManager::resyncDeviceModifiers(TrackId trackId) {
    // Check if any device has new links that don't have TE modifiers yet
    bool needsFullSync = false;
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (trackInfo) {
        for (const auto& element : trackInfo->chainElements) {
            if (!isDevice(element))
                continue;
            const auto& device = getDevice(element);
            int activeModCount = 0;
            for (const auto& mod : device.mods) {
                if (mod.enabled && !mod.links.empty())
                    activeModCount++;
            }
            auto it = deviceModifiers_.find(device.id);
            int existingCount =
                (it != deviceModifiers_.end()) ? static_cast<int>(it->second.size()) : 0;
            if (activeModCount != existingCount) {
                needsFullSync = true;
                break;
            }
        }
    }

    if (needsFullSync) {
        // New links added or removed — need full modifier rebuild
        auto* teTrack = trackController_.getAudioTrack(trackId);
        if (teTrack)
            syncDeviceModifiers(trackId, teTrack);
        rackSyncManager_.resyncAllModifiers(trackId);
    } else {
        // Just update properties on existing modifiers in-place
        updateDeviceModifierProperties(trackId);
        rackSyncManager_.updateAllModifierProperties(trackId);
    }

    rebuildSidechainLFOCache();
}

// =============================================================================
// Macro Value Routing
// =============================================================================

void PluginManager::setMacroValue(TrackId trackId, bool isRack, int id, int macroIndex,
                                  float value) {
    if (isRack) {
        // Rack macro — delegate to RackSyncManager
        rackSyncManager_.setMacroValue(static_cast<RackId>(id), macroIndex, value);
    } else {
        // Device macro — use device macro params
        setDeviceMacroValue(static_cast<DeviceId>(id), macroIndex, value);
    }
}

void PluginManager::setDeviceMacroValue(DeviceId deviceId, int macroIndex, float value) {
    auto it = deviceMacroParams_.find(deviceId);
    if (it == deviceMacroParams_.end())
        return;

    auto macroIt = it->second.find(macroIndex);
    if (macroIt != it->second.end() && macroIt->second != nullptr) {
        macroIt->second->setParameter(value, juce::sendNotificationSync);
    }
}

void PluginManager::syncDeviceMacros(TrackId trackId, te::AudioTrack* teTrack) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    // Get the track's MacroParameterList (used for both cleanup and creation)
    auto& macroList = teTrack->getMacroParameterListForWriting();

    // Remove existing TE MacroParameters before recreating
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;
        const auto& device = getDevice(element);

        auto it = deviceMacroParams_.find(device.id);
        if (it != deviceMacroParams_.end()) {
            for (auto& [macroIdx, macroParam] : it->second) {
                if (!macroParam)
                    continue;

                // Remove modifier assignments from all plugin params on this track
                for (const auto& el : trackInfo->chainElements) {
                    if (!isDevice(el))
                        continue;
                    const auto& dev = getDevice(el);
                    te::Plugin::Ptr plugin;
                    {
                        juce::ScopedLock lock(pluginLock_);
                        auto pit = deviceToPlugin_.find(dev.id);
                        if (pit != deviceToPlugin_.end())
                            plugin = pit->second;
                    }
                    if (plugin) {
                        for (auto* param : plugin->getAutomatableParameters())
                            param->removeModifier(*macroParam);
                    }
                }

                macroList.removeMacroParameter(*macroParam);
            }
            deviceMacroParams_.erase(it);
        }
    }

    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);

        for (int i = 0; i < static_cast<int>(device.macros.size()); ++i) {
            const auto& macroInfo = device.macros[static_cast<size_t>(i)];
            if (!macroInfo.isLinked())
                continue;

            // Create a TE MacroParameter
            auto* macroParam = macroList.createMacroParameter();
            if (!macroParam)
                continue;

            macroParam->macroName = macroInfo.name;
            macroParam->setParameter(macroInfo.value, juce::dontSendNotification);

            deviceMacroParams_[device.id][i] = macroParam;

            // Create assignments for each link
            for (const auto& link : macroInfo.links) {
                if (!link.target.isValid())
                    continue;

                // Find the TE plugin for the link target device
                te::Plugin::Ptr linkTarget;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = deviceToPlugin_.find(link.target.deviceId);
                    if (pit != deviceToPlugin_.end())
                        linkTarget = pit->second;
                }
                if (!linkTarget)
                    continue;

                auto params = linkTarget->getAutomatableParameters();
                if (link.target.paramIndex >= 0 &&
                    link.target.paramIndex < static_cast<int>(params.size())) {
                    auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                    if (param) {
                        param->addModifier(*macroParam, link.amount);
                        DBG("syncDeviceMacros: Linked macro "
                            << i << " on device " << device.id << " to device "
                            << link.target.deviceId << " param " << link.target.paramIndex);
                    }
                }
            }
        }
    }
}

// =============================================================================
// Sidechain Routing Sync
// =============================================================================

void PluginManager::syncSidechains(TrackId trackId, te::AudioTrack* teTrack) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);
        auto plugin = getPlugin(device.id);
        if (!plugin || !plugin->canSidechain())
            continue;

        if (device.sidechain.isActive() && device.sidechain.type == SidechainConfig::Type::Audio) {
            auto* sourceTrack = trackController_.getAudioTrack(device.sidechain.sourceTrackId);
            if (sourceTrack) {
                plugin->setSidechainSourceID(sourceTrack->itemID);
                plugin->guessSidechainRouting();
            }
        } else {
            // Clear sidechain if not active
            plugin->setSidechainSourceID({});
        }
    }
}

// =============================================================================
// Sidechain Monitor Lifecycle
// =============================================================================

bool PluginManager::trackNeedsSidechainMonitor(TrackId trackId) const {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return false;

    // Check if this track has any MIDI-triggered mods (self-trigger).
    // Audio-triggered mods don't need the monitor — audio peaks come from
    // LevelMeterPlugin via AudioBridge timer, not from this plugin.
    for (const auto& element : trackInfo->chainElements) {
        if (isDevice(element)) {
            for (const auto& mod : getDevice(element).mods) {
                if (mod.triggerMode == LFOTriggerMode::MIDI)
                    return true;
            }
        } else if (isRack(element)) {
            for (const auto& mod : getRack(element).mods) {
                if (mod.triggerMode == LFOTriggerMode::MIDI)
                    return true;
            }
        }
    }

    // Check if this track is a MIDI sidechain source for any other track
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        for (const auto& element : track.chainElements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                if (device.sidechain.type == SidechainConfig::Type::MIDI &&
                    device.sidechain.sourceTrackId == trackId) {
                    return true;
                }
            }
        }
    }

    return false;
}

void PluginManager::checkSidechainMonitor(TrackId trackId) {
    if (trackNeedsSidechainMonitor(trackId))
        ensureSidechainMonitor(trackId);
    else
        removeSidechainMonitor(trackId);

    rebuildSidechainLFOCache();
}

void PluginManager::ensureSidechainMonitor(TrackId sourceTrackId) {
    // Already have a monitor for this track?
    if (sidechainMonitors_.count(sourceTrackId) > 0) {
        DBG("PluginManager::ensureSidechainMonitor - track " << sourceTrackId
                                                             << " already has monitor");
        return;
    }

    auto* teTrack = trackController_.getAudioTrack(sourceTrackId);
    if (!teTrack) {
        DBG("PluginManager::ensureSidechainMonitor - track " << sourceTrackId
                                                             << " has no TE AudioTrack");
        return;
    }

    // Check if a SidechainMonitorPlugin already exists on the track
    for (int i = 0; i < teTrack->pluginList.size(); ++i) {
        if (dynamic_cast<SidechainMonitorPlugin*>(teTrack->pluginList[i])) {
            DBG("PluginManager::ensureSidechainMonitor - track "
                << sourceTrackId << " found existing monitor plugin on TE track");
            sidechainMonitors_[sourceTrackId] = teTrack->pluginList[i];
            auto* mon = dynamic_cast<SidechainMonitorPlugin*>(teTrack->pluginList[i]);
            mon->setSourceTrackId(sourceTrackId);
            mon->setPluginManager(this);
            return;
        }
    }

    // Create a new monitor plugin via TE's plugin cache (uses createCustomPlugin)
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, SidechainMonitorPlugin::xmlTypeName, nullptr);
    pluginState.setProperty(juce::Identifier("sourceTrackId"), sourceTrackId, nullptr);

    DBG("PluginManager::ensureSidechainMonitor - creating new monitor for track " << sourceTrackId);
    auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    if (plugin) {
        if (auto* mon = dynamic_cast<SidechainMonitorPlugin*>(plugin.get())) {
            mon->setSourceTrackId(sourceTrackId);
            mon->setPluginManager(this);
        }
        // Insert at position 0 so it sees MIDI before the instrument consumes it.
        // Audio peak detection is handled separately via LevelMeterPlugin.
        teTrack->pluginList.insertPlugin(plugin, 0, nullptr);
        sidechainMonitors_[sourceTrackId] = plugin;
        DBG("PluginManager::ensureSidechainMonitor - inserted monitor at position 0 on track "
            << sourceTrackId);
    } else {
        DBG("PluginManager::ensureSidechainMonitor - FAILED to create monitor plugin for track "
            << sourceTrackId);
    }
}

void PluginManager::removeSidechainMonitor(TrackId sourceTrackId) {
    auto it = sidechainMonitors_.find(sourceTrackId);
    if (it == sidechainMonitors_.end())
        return;

    DBG("PluginManager::removeSidechainMonitor - removing monitor from track " << sourceTrackId);
    auto plugin = it->second;
    sidechainMonitors_.erase(it);

    if (plugin)
        plugin->deleteFromParent();
}

// Utilities
// =============================================================================

void PluginManager::clearAllMappings() {
    juce::ScopedLock lock(pluginLock_);
    instrumentRackManager_.clear();
    rackSyncManager_.clear();
    deviceModifiers_.clear();
    deviceMacroParams_.clear();
    deviceToPlugin_.clear();
    pluginToDevice_.clear();
    deviceProcessors_.clear();
    sidechainMonitors_.clear();
}

void PluginManager::updateTransportSyncedProcessors(bool isPlaying) {
    juce::ScopedLock lock(pluginLock_);

    for (const auto& [deviceId, processor] : deviceProcessors_) {
        if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(processor.get())) {
            // Test Tone is always transport-synced
            // Simply bypass when stopped, enable when playing
            toneProc->setBypassed(!isPlaying);
        }
    }
}

// =============================================================================
// Rack Plugin Creation
// =============================================================================

te::Plugin::Ptr PluginManager::createPluginOnly(TrackId trackId, const DeviceInfo& device) {
    DBG("createPluginOnly: device='" << device.name << "' format=" << device.getFormatString());

    te::Plugin::Ptr plugin;

    if (device.format == PluginFormat::Internal) {
        if (device.pluginId.containsIgnoreCase("delay")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::DelayPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("reverb")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::ReverbPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("eq")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::EqualiserPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("compressor")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::CompressorPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("chorus")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::ChorusPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("phaser")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::PhaserPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("tone")) {
            plugin =
                edit_.getPluginCache().createNewPlugin(te::ToneGeneratorPlugin::xmlTypeName, {});
        } else if (device.pluginId.containsIgnoreCase("4osc")) {
            plugin = edit_.getPluginCache().createNewPlugin(te::FourOscPlugin::xmlTypeName, {});
        }
    } else {
        // External plugin — same lookup logic as loadDeviceAsPlugin but without track insertion
        if (device.uniqueId.isNotEmpty() || device.fileOrIdentifier.isNotEmpty()) {
            juce::PluginDescription desc;
            desc.name = device.name;
            desc.manufacturerName = device.manufacturer;
            desc.fileOrIdentifier = device.fileOrIdentifier;
            desc.isInstrument = device.isInstrument;

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
            auto& knownPlugins = engine_.getPluginManager().knownPluginList;
            bool found = false;

            for (const auto& knownDesc : knownPlugins.getTypes()) {
                if (knownDesc.fileOrIdentifier == device.fileOrIdentifier &&
                    knownDesc.isInstrument == device.isInstrument) {
                    desc = knownDesc;
                    found = true;
                    break;
                }
            }

            if (!found) {
                for (const auto& knownDesc : knownPlugins.getTypes()) {
                    if (knownDesc.name == device.name &&
                        knownDesc.manufacturerName == device.manufacturer &&
                        knownDesc.isInstrument == device.isInstrument) {
                        desc = knownDesc;
                        found = true;
                        break;
                    }
                }
            }

            // Apply TE bug workaround (same as loadExternalPlugin)
            juce::PluginDescription descCopy = desc;
            if (descCopy.deprecatedUid != 0) {
                descCopy.uniqueId = 0;
            }

            plugin =
                edit_.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, descCopy);
        }
    }

    if (plugin) {
        plugin->setEnabled(!device.bypassed);
    }

    return plugin;
}

// =============================================================================
// Internal Implementation
// =============================================================================

te::Plugin::Ptr PluginManager::loadDeviceAsPlugin(TrackId trackId, const DeviceInfo& device) {
    auto* track = trackController_.getAudioTrack(trackId);
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
        // Update canSidechain flag on the DeviceInfo in TrackManager
        if (plugin->canSidechain()) {
            if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, device.id)) {
                devInfo->canSidechain = true;
            }
        }

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

        // Wrap instruments in a RackType with audio passthrough so both synth
        // output and audio clips on the same track are summed together
        if (device.isInstrument) {
            auto rackPlugin = instrumentRackManager_.wrapInstrument(plugin);
            if (rackPlugin) {
                // Record the wrapping so we can look up the inner plugin later
                auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
                te::RackType::Ptr rackType = rackInstance ? rackInstance->type : nullptr;
                instrumentRackManager_.recordWrapping(device.id, rackType, plugin, rackPlugin);

                // Insert the rack instance on the track
                // The raw plugin is already inside the rack (added by wrapInstrument)
                track->pluginList.insertPlugin(rackPlugin, -1, nullptr);

                std::cout << "Loaded instrument device " << device.id << " (" << device.name
                          << ") wrapped in rack" << std::endl;

                // Return the INNER plugin (not the rack) so that deviceToPlugin_
                // maps to the actual synth for parameter access and window opening
                return plugin;
            }
            // Fallback: if wrapping failed, the plugin was already removed from the
            // track by wrapInstrument, so re-insert it directly
            track->pluginList.insertPlugin(plugin, -1, nullptr);
            std::cerr << "InstrumentRackManager: Wrapping failed for " << device.name
                      << ", using raw plugin" << std::endl;
        }

        // For tone generators (always transport-synced), sync initial state with transport
        if (auto* toneProc = deviceProcessors_[device.id].get()) {
            if (auto* toneGen = dynamic_cast<ToneGeneratorProcessor*>(toneProc)) {
                // Get current transport state
                bool isPlaying = transportState_.isPlaying();
                // Bypass if transport is not playing
                toneGen->setBypassed(!isPlaying);
            }
        }

        std::cout << "Loaded device " << device.id << " (" << device.name << ") as plugin"
                  << std::endl;

        // Note: Auto-routing MIDI for instruments is handled by AudioBridge
        // (coordination logic, not plugin management responsibility)
    }

    return plugin;
}

// =============================================================================
// Plugin Creation Helpers
// =============================================================================

te::Plugin::Ptr PluginManager::createToneGenerator(te::AudioTrack* track) {
    if (!track)
        return nullptr;

    // Create tone generator plugin via PluginCache
    // ToneGeneratorProcessor will handle parameter configuration
    auto plugin = edit_.getPluginCache().createNewPlugin(te::ToneGeneratorPlugin::xmlTypeName, {});
    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);
        DBG("PluginManager::createToneGenerator - Created tone generator on track: " +
            track->getName());
        DBG("  Plugin enabled: " << (plugin->isEnabled() ? "YES" : "NO"));
        if (auto* outputDevice = track->getOutput().getOutputDevice(false)) {
            DBG("  Track output device: " + outputDevice->getName());
        } else {
            DBG("  Track output device: NULL!");
        }
    } else {
        DBG("PluginManager::createToneGenerator - FAILED to create tone generator!");
    }
    return plugin;
}

te::Plugin::Ptr PluginManager::createLevelMeter(te::AudioTrack* track) {
    if (!track)
        return nullptr;

    // LevelMeterPlugin has create() that returns ValueTree
    auto plugin = edit_.getPluginCache().createNewPlugin(te::LevelMeterPlugin::create());
    if (plugin) {
        track->pluginList.insertPlugin(plugin, -1, nullptr);
    }
    return plugin;
}

te::Plugin::Ptr PluginManager::createFourOscSynth(te::AudioTrack* track) {
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

}  // namespace magda
