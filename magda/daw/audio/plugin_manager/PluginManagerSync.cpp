#include <set>
#include <unordered_set>
#include <vector>

#include "../../core/RackInfo.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/aliases/AutoAliasGenerator.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../PluginWindowBridge.hpp"
#include "../TrackController.hpp"
#include "../TracktionHelpers.hpp"
#include "PluginManager.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "modifiers/ModifierHelpers.hpp"
#include "modifiers/ModifierSync.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/AudioSidechainMonitorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

// =============================================================================
// Plugin Synchronization
// =============================================================================

void PluginManager::syncAllPlugins() {
    auto& tm = TrackManager::getInstance();
    const auto& tracks = tm.getTracks();

    // ── Step 1: Collect all valid device/rack IDs across ALL tracks ──────
    std::set<DeviceId> validDeviceIds;
    std::set<RackId> validRackIds;

    std::function<void(const std::vector<ChainElement>&)> collectIds;
    collectIds = [&](const std::vector<ChainElement>& elements) {
        for (const auto& element : elements) {
            if (isDevice(element)) {
                validDeviceIds.insert(getDevice(element).id);
            } else if (isRack(element)) {
                const auto& rack = getRack(element);
                validRackIds.insert(rack.id);
                for (const auto& chain : rack.chains)
                    collectIds(chain.elements);
            }
        }
    };

    for (const auto& track : tracks) {
        collectIds(track.chainElements);
    }

    // Include master track (not in getTracks())
    if (auto* masterTrack = tm.getTrack(MASTER_TRACK_ID)) {
        collectIds(masterTrack->chainElements);
    }

    // ── Step 2: Remove orphan devices (globally) ────────────────────────
    {
        std::vector<DeviceId> orphanDevices;
        std::vector<te::Plugin::Ptr> pluginsToDelete;
        {
            juce::ScopedLock lock(pluginLock_);
            deferredHolders_.clear();  // Drain previous cycle's deferred holders
            for (auto it = syncedDevices_.begin(); it != syncedDevices_.end();) {
                if (validDeviceIds.find(it->first) == validDeviceIds.end()) {
                    clearLFOCustomWaveCallbacks(it->second.modifiers);
                    deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                    if (auto* dg =
                            dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get()))
                        dg->removeListener(this);
                    if (it->second.plugin)
                        pluginToDevice_.erase(it->second.plugin.get());
                    if (it->second.midiReceivePlugin)
                        pluginsToDelete.push_back(it->second.midiReceivePlugin);
                    orphanDevices.push_back(it->first);
                    if (it->second.plugin)
                        pluginsToDelete.push_back(it->second.plugin);
                    it = syncedDevices_.erase(it);
                } else {
                    ++it;
                }
            }

            // Also purge stale sidechain monitors
            for (auto it = sidechainMonitors_.begin(); it != sidechainMonitors_.end();) {
                auto trackExists = std::any_of(tracks.begin(), tracks.end(),
                                               [&](const auto& t) { return t.id == it->first; });
                if (!trackExists) {
                    if (it->second)
                        pluginsToDelete.push_back(it->second);
                    it = sidechainMonitors_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Close windows and delete plugins outside lock
        for (auto deviceId : orphanDevices) {
            pluginWindowBridge_.closeWindowsForDevice(deviceId);
            if (instrumentRackManager_.getInnerPlugin(deviceId) != nullptr)
                instrumentRackManager_.unwrap(deviceId);
        }
        for (auto& plugin : pluginsToDelete)
            plugin->deleteFromParent();

        if (!orphanDevices.empty())
            DBG("syncAllPlugins: removed " << (int)orphanDevices.size() << " orphan devices");
    }

    // ── Step 3: Remove orphan racks (globally) ──────────────────────────
    {
        auto syncedRackIds = rackSyncManager_.getSyncedRackIds();
        int removedRacks = 0;
        for (auto rackId : syncedRackIds) {
            if (validRackIds.find(rackId) == validRackIds.end()) {
                rackSyncManager_.removeRack(rackId);
                ++removedRacks;
            }
        }
        if (removedRacks > 0)
            DBG("syncAllPlugins: removed " << removedRacks << " orphan racks");
    }

    // ── Step 4: Per-track additive sync (including master) ─────────────
    for (const auto& track : tracks) {
        syncTrackPlugins(track.id);
    }
    syncTrackPlugins(MASTER_TRACK_ID);

    // ── Step 5: Rebuild sidechain LFO cache once at the end ─────────────
    rebuildSidechainLFOCache();
}

void PluginManager::syncTrackPlugins(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    // MultiOut tracks have a special sync path
    if (trackInfo->type == TrackType::MultiOut) {
        syncMultiOutTrack(trackId, *trackInfo);
        return;
    }

    // Master track uses the Edit's master plugin list
    if (trackId == MASTER_TRACK_ID) {
        syncMasterPlugins();
        return;
    }

    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (!teTrack) {
        teTrack = trackController_.createAudioTrack(trackId, trackInfo->name);
    }

    if (!teTrack)
        return;

    // Get current MAGDA devices and racks from chain elements (recursive).
    // Devices inside racks must be included so that wrapping a device in a
    // rack doesn't cause the sync logic to delete and recreate the TE plugin
    // (which resets all plugin state).
    std::vector<DeviceId> magdaDevices;
    std::vector<RackId> magdaRacks;
    std::function<void(const std::vector<ChainElement>&)> collectElements =
        [&](const std::vector<ChainElement>& elements) {
            for (const auto& element : elements) {
                if (isDevice(element)) {
                    magdaDevices.push_back(getDevice(element).id);
                } else if (isRack(element)) {
                    magdaRacks.push_back(getRack(element).id);
                    for (const auto& chain : getRack(element).chains) {
                        collectElements(chain.elements);
                    }
                }
            }
        };
    collectElements(trackInfo->chainElements);

    // Remove TE plugins that no longer exist in MAGDA for THIS track.
    // Uses the stored trackId for ownership — no TE owner-track heuristic needed.
    std::vector<DeviceId> toRemove;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (!sd.plugin || sd.trackId != trackId)
                continue;

            bool found =
                std::find(magdaDevices.begin(), magdaDevices.end(), deviceId) != magdaDevices.end();
            if (!found) {
                toRemove.push_back(deviceId);
                pluginsToDelete.push_back(sd.plugin);
            }
        }

        // Remove from mappings while under lock
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (auto deviceId : toRemove) {
            auto it = syncedDevices_.find(deviceId);
            if (it != syncedDevices_.end()) {
                // Clear LFO callbacks before destroying CurveSnapshotHolders
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
                    dg->removeListener(this);
                    // Remove pad plugin entries for this DrumGrid
                    auto padIt = drumGridPadDevices_.find(deviceId);
                    if (padIt != drumGridPadDevices_.end()) {
                        for (auto padDevId : padIt->second) {
                            auto padSdIt = syncedDevices_.find(padDevId);
                            if (padSdIt != syncedDevices_.end()) {
                                if (padSdIt->second.plugin)
                                    pluginToDevice_.erase(padSdIt->second.plugin.get());
                                syncedDevices_.erase(padSdIt);
                            }
                        }
                        drumGridPadDevices_.erase(padIt);
                    }
                }
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                syncedDevices_.erase(it);
            }
        }
    }

    // Delete plugins outside lock to avoid blocking other threads
    for (size_t i = 0; i < toRemove.size(); ++i) {
        pluginWindowBridge_.closeWindowsForDevice(toRemove[i]);

        // Remove any orphaned MidiReceivePlugin for this device
        removeMidiReceive(trackId, toRemove[i]);

        // If this was a wrapped instrument, unwrap it (removes rack + rack type)
        if (instrumentRackManager_.getInnerPlugin(toRemove[i]) != nullptr) {
            instrumentRackManager_.unwrap(toRemove[i]);
        } else if (pluginsToDelete[i]) {
            pluginsToDelete[i]->deleteFromParent();
        }
    }

    // Remove stale racks on THIS track (racks no longer in MAGDA chain elements).
    // Only check racks belonging to this track — not racks on other tracks.
    // RackInstances are tracked by RackSyncManager, not in syncedDevices_,
    // so we query the synced rack IDs directly.
    {
        auto syncedIds = rackSyncManager_.getSyncedRackIdsForTrack(trackId);
        for (auto rackId : syncedIds) {
            if (std::find(magdaRacks.begin(), magdaRacks.end(), rackId) == magdaRacks.end()) {
                rackSyncManager_.removeRack(rackId);
            }
        }
    }

    // Add new plugins for MAGDA devices that don't have TE counterparts
    for (size_t elemIdx = 0; elemIdx < trackInfo->chainElements.size(); ++elemIdx) {
        const auto& element = trackInfo->chainElements[elemIdx];
        if (isDevice(element)) {
            const auto& device = getDevice(element);

            juce::ScopedLock lock(pluginLock_);
            if (syncedDevices_.find(device.id) == syncedDevices_.end()) {
                // Compute TE insertion index: find the first subsequent chain element
                // that already has a synced plugin, and insert before it.
                int teInsertIndex = -1;  // -1 = append (before VolumeAndPan/LevelMeter)
                auto* teTrackForIdx = trackController_.getAudioTrack(trackId);
                for (size_t j = elemIdx + 1; teTrackForIdx && j < trackInfo->chainElements.size();
                     ++j) {
                    if (isDevice(trackInfo->chainElements[j])) {
                        auto nextId = getDevice(trackInfo->chainElements[j]).id;
                        auto it = syncedDevices_.find(nextId);
                        if (it != syncedDevices_.end() && it->second.plugin) {
                            // For wrapped instruments, the actual plugin on the track
                            // is the RackInstance, not the inner plugin.
                            auto* rackInst = instrumentRackManager_.getRackInstance(nextId);
                            auto* pluginOnTrack = rackInst ? rackInst : it->second.plugin.get();
                            int idx = teTrackForIdx->pluginList.indexOf(pluginOnTrack);
                            if (idx >= 0) {
                                teInsertIndex = idx;
                                break;
                            }
                        }
                    }
                }

                auto plugin = loadDeviceAsPlugin(trackId, device, teInsertIndex);
                if (plugin) {
                    syncedDevices_[device.id].trackId = trackId;
                    syncedDevices_[device.id].plugin = plugin;
                    pluginToDevice_[plugin.get()] = device.id;

                    // Check if plugin is still loading asynchronously (external plugins)
                    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                        if (extPlugin->isInitialisingAsync()) {
                            syncedDevices_[device.id].isPendingLoad = true;

                            if (auto* devInfo =
                                    TrackManager::getInstance().getDevice(trackId, device.id)) {
                                devInfo->loadState = DeviceLoadState::Loading;
                            }

                            // Notify so UI rebuilds with the Loading indicator
                            TrackManager::getInstance().notifyTrackDevicesChanged(trackId);

                            // Poll for completion — TE's async callback runs on message
                            // thread, so a short timer will catch it promptly
                            auto deviceId = device.id;
                            pollAsyncPluginLoad(trackId, deviceId, plugin);
                        }
                    }
                }
            }
        } else if (isRack(element)) {
            const auto& rackInfo = getRack(element);
            DBG("syncTrackPlugins: found rack id=" << rackInfo.id
                                                   << " chains=" << (int)rackInfo.chains.size()
                                                   << " totalDevices=" << [&]() {
                                                          int n = 0;
                                                          for (auto& c : rackInfo.chains)
                                                              for (auto& e : c.elements)
                                                                  if (isDevice(e))
                                                                      n++;
                                                          return n;
                                                      }());

            // Unwrap any InstrumentRackManager wrappers for devices that moved
            // into this MAGDA rack.  The standalone wrapper must be removed before
            // RackSyncManager creates its own rack containing the same device.
            // We need a mutable RackInfo to write captured state into the DeviceInfo
            // that createPluginOnly will read.
            auto* mutableRack = TrackManager::getInstance().getRack(trackId, rackInfo.id);
            jassert(mutableRack != nullptr);
            if (!mutableRack)
                continue;
            for (auto& chain : mutableRack->chains) {
                for (auto& chainElement : chain.elements) {
                    if (isDevice(chainElement)) {
                        auto& devInfo = getDevice(chainElement);
                        auto devId = devInfo.id;
                        if (auto* innerPlugin = instrumentRackManager_.getInnerPlugin(devId)) {
                            // Capture the plugin's current state before unwrapping
                            // so RackSyncManager can restore it in the new rack plugin
                            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(innerPlugin)) {
                                ext->flushPluginStateToValueTree();
                                devInfo.pluginState =
                                    ext->state.getProperty(te::IDs::state).toString();
                            } else {
                                auto stateCopy = innerPlugin->state.createCopy();
                                stateCopy.removeProperty(te::IDs::id, nullptr);
                                if (auto xml = stateCopy.createXml())
                                    devInfo.pluginState = xml->toString();
                            }
                            DBG("syncTrackPlugins: captured state for device "
                                << devId << " len=" << devInfo.pluginState.length());

                            DBG("syncTrackPlugins: unwrapping InstrumentRack for device "
                                << devId << " (moved into MAGDA rack " << rackInfo.id << ")");
                            instrumentRackManager_.unwrap(devId);

                            // Also remove from syncedDevices_ so it doesn't conflict
                            juce::ScopedLock lock(pluginLock_);
                            auto sdIt = syncedDevices_.find(devId);
                            if (sdIt != syncedDevices_.end()) {
                                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(
                                        sdIt->second.plugin.get())) {
                                    dg->removeListener(this);
                                    auto padIt = drumGridPadDevices_.find(devId);
                                    if (padIt != drumGridPadDevices_.end()) {
                                        for (auto padDevId : padIt->second) {
                                            auto padSdIt = syncedDevices_.find(padDevId);
                                            if (padSdIt != syncedDevices_.end()) {
                                                if (padSdIt->second.plugin)
                                                    pluginToDevice_.erase(
                                                        padSdIt->second.plugin.get());
                                                syncedDevices_.erase(padSdIt);
                                            }
                                        }
                                        drumGridPadDevices_.erase(padIt);
                                    }
                                }
                                if (sdIt->second.plugin)
                                    pluginToDevice_.erase(sdIt->second.plugin.get());
                                syncedDevices_.erase(sdIt);
                            }
                        }
                    }
                }
            }

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
                                syncedDevices_[device.id].trackId = trackId;
                                syncedDevices_[device.id].plugin = innerPlugin;
                                pluginToDevice_[innerPlugin] = device.id;
                            }
                        }
                    }
                }
            }
        }
    }

    // Any track with auxBusIndex: ensure AuxReturnPlugin exists with correct bus number
    if (trackInfo->auxBusIndex >= 0) {
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

    // Sync device-level + track-level modifiers AND macros via the
    // ModifierSyncWalker (issue #1131 step 2).
    syncDeviceModifiers(trackId, teTrack);

    // Update mod link fingerprint so resyncDeviceModifiers doesn't rebuild immediately after
    if (auto* info = TrackManager::getInstance().getTrack(trackId))
        modLinkFingerprints_[trackId] = computeModLinkFingerprint(trackId, info);

    // Sync sidechain routing for plugins that support it
    syncSidechains(trackId, teTrack);

    // Sidechain monitors: insert on tracks that are sidechain sources.
    // MIDI monitor at position 0 (before instruments), audio monitor near end (after instruments).
    if (trackNeedsSidechainMonitor(trackId))
        ensureSidechainMonitor(trackId);
    else
        removeSidechainMonitor(trackId);

    if (trackNeedsAudioSidechainMonitor(trackId))
        ensureAudioSidechainMonitor(trackId);
    else
        removeAudioSidechainMonitor(trackId);

    // Reorder TE plugins to match the MAGDA chain element order.
    // This handles moveNode (drag-and-drop reorder) where the MAGDA chain changed
    // but existing TE plugins haven't moved.
    {
        // Build the desired order of TE plugin indices from the MAGDA chain
        std::vector<te::Plugin*> desiredOrder;
        for (const auto& element : trackInfo->chainElements) {
            if (isDevice(element)) {
                juce::ScopedLock lock(pluginLock_);
                auto it = syncedDevices_.find(getDevice(element).id);
                if (it != syncedDevices_.end() && it->second.plugin) {
                    // For instrument-rack-wrapped plugins, find the rack instance on the track
                    auto* wrapped = instrumentRackManager_.getRackInstance(it->first);
                    auto* pluginToFind = wrapped ? wrapped : it->second.plugin.get();
                    if (teTrack->pluginList.indexOf(pluginToFind) >= 0)
                        desiredOrder.push_back(pluginToFind);
                }
            } else if (isRack(element)) {
                auto* rackInstance = rackSyncManager_.getRackInstance(getRack(element).id);
                if (rackInstance && teTrack->pluginList.indexOf(rackInstance) >= 0)
                    desiredOrder.push_back(rackInstance);
            }
        }

        // Walk the desired order and move each plugin to its correct position
        // using ValueTree::moveChild on the plugin list's state.
        auto& listState = teTrack->pluginList.state;
        for (size_t i = 0; i < desiredOrder.size(); ++i) {
            int currentIdx = teTrack->pluginList.indexOf(desiredOrder[i]);
            // Find the ValueTree child index for this plugin
            int vtChildIdx = listState.indexOf(desiredOrder[i]->state);
            if (vtChildIdx < 0 || currentIdx < 0)
                continue;

            // Find where it should go: after the previous desired plugin's VT child
            if (i == 0) {
                // First user plugin: move after any fixed front-of-chain plugins
                // (SidechainMonitorPlugin, AuxReturn) that must stay at the start.
                int targetVtIdx = 0;
                for (int c = 0; c < listState.getNumChildren(); ++c) {
                    auto child = listState.getChild(c);
                    if (child.hasType(te::IDs::PLUGIN)) {
                        auto type = child.getProperty(te::IDs::type).toString();
                        if (type == "auxreturn" || type == SidechainMonitorPlugin::xmlTypeName)
                            targetVtIdx = c + 1;
                    }
                }
                if (vtChildIdx != targetVtIdx)
                    listState.moveChild(vtChildIdx, targetVtIdx, nullptr);
            } else {
                // Move after the previous desired plugin
                int prevVtIdx = listState.indexOf(desiredOrder[i - 1]->state);
                int curVtIdx = listState.indexOf(desiredOrder[i]->state);
                if (curVtIdx >= 0 && prevVtIdx >= 0 && curVtIdx != prevVtIdx + 1)
                    listState.moveChild(curVtIdx, prevVtIdx + 1, nullptr);
            }
        }
    }

    // Register DrumGrid pad plugins in syncedDevices_ for macro/mod linking
    {
        std::vector<std::pair<DeviceId, daw::audio::DrumGridPlugin*>> drumGrids;
        {
            juce::ScopedLock lock(pluginLock_);
            for (const auto& [deviceId, sd] : syncedDevices_) {
                if (sd.trackId != trackId)
                    continue;
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(sd.plugin.get()))
                    drumGrids.push_back({deviceId, dg});
            }
        }
        for (auto& [deviceId, dg] : drumGrids)
            syncDrumGridPadPlugins(trackId, deviceId, dg);
    }

    // Ensure VolumeAndPan is near the end of the chain (before LevelMeter)
    // This is the track's fader control - it should come AFTER audio sources
    ensureVolumePluginPosition(teTrack);

    // Ensure LevelMeter is at the end of the plugin chain for metering
    addLevelMeterToTrack(trackId);

    // Rebuild sidechain LFO cache so audio/MIDI threads see current state
    rebuildSidechainLFOCache();
}

// =============================================================================
// Track Deletion Cleanup
// =============================================================================

void PluginManager::cleanupTrackPlugins(TrackId trackId) {
    // 1. Collect DeviceIds belonging to this track using stored trackId
    std::vector<DeviceId> deviceIds;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (sd.trackId != trackId)
                continue;

            deviceIds.push_back(deviceId);
            if (sd.plugin)
                pluginsToDelete.push_back(sd.plugin);
            if (sd.midiReceivePlugin)
                pluginsToDelete.push_back(sd.midiReceivePlugin);
        }

        // 2. Erase map entries for collected DeviceIds
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (auto deviceId : deviceIds) {
            auto it = syncedDevices_.find(deviceId);
            if (it != syncedDevices_.end()) {
                // Clear LFO callbacks before destroying CurveSnapshotHolders
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
                    dg->removeListener(this);
                    auto padIt = drumGridPadDevices_.find(deviceId);
                    if (padIt != drumGridPadDevices_.end()) {
                        for (auto padDevId : padIt->second) {
                            auto padSdIt = syncedDevices_.find(padDevId);
                            if (padSdIt != syncedDevices_.end()) {
                                if (padSdIt->second.plugin)
                                    pluginToDevice_.erase(padSdIt->second.plugin.get());
                                syncedDevices_.erase(padSdIt);
                            }
                        }
                        drumGridPadDevices_.erase(padIt);
                    }
                }
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                syncedDevices_.erase(it);
            }
        }
    }

    // 3. Delete plugins and close windows outside lock
    for (size_t i = 0; i < deviceIds.size(); ++i) {
        pluginWindowBridge_.closeWindowsForDevice(deviceIds[i]);

        // Remove any MidiReceivePlugin for this device
        removeMidiReceive(trackId, deviceIds[i]);

        // Unwrap instrument racks
        if (instrumentRackManager_.getInnerPlugin(deviceIds[i]) != nullptr) {
            instrumentRackManager_.unwrap(deviceIds[i]);
        } else if (pluginsToDelete[i]) {
            pluginsToDelete[i]->deleteFromParent();
        }
    }

    // 4. Remove sidechain monitor for this track
    removeSidechainMonitor(trackId);

    // 5. Remove all racks belonging to this track
    rackSyncManager_.removeRacksForTrack(trackId);

    // 5b. Clean up track-level mod state
    {
        auto tmIt = trackModStates_.find(trackId);
        if (tmIt != trackModStates_.end()) {
            clearLFOCustomWaveCallbacks(tmIt->second.modifiers);
            deferCurveSnapshots(tmIt->second.curveSnapshots, deferredHolders_);
            trackModStates_.erase(tmIt);
        }
    }

    // 5c. Clean up track-level macro state
    trackMacroParams_.erase(trackId);
    modLinkFingerprints_.erase(trackId);

    // 6. Clean up cross-track references (Stage 2)
    // Remove MidiReceivePlugins on other tracks that reference the deleted track as source
    {
        std::vector<DeviceId> midiReceiveToRemove;
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (sd.midiReceivePlugin) {
                if (auto* rx = dynamic_cast<MidiReceivePlugin*>(sd.midiReceivePlugin.get())) {
                    if (rx->getSourceTrackId() == trackId)
                        midiReceiveToRemove.push_back(deviceId);
                }
            }
        }
        for (auto devId : midiReceiveToRemove) {
            auto it = syncedDevices_.find(devId);
            if (it != syncedDevices_.end()) {
                auto plugin = it->second.midiReceivePlugin;
                it->second.midiReceivePlugin = nullptr;
                if (plugin)
                    plugin->deleteFromParent();
            }
        }
    }

    // Clear audio sidechain sources on other tracks' plugins referencing deleted track
    {
        auto& tm = TrackManager::getInstance();
        for (const auto& track : tm.getTracks()) {
            if (track.id == trackId)
                continue;
            for (const auto& element : track.chainElements) {
                if (!isDevice(element))
                    continue;
                const auto& device = getDevice(element);
                if (device.sidechain.isActive() && device.sidechain.sourceTrackId == trackId) {
                    auto plugin = getPlugin(device.id);
                    if (plugin && plugin->canSidechain()) {
                        plugin->setSidechainSourceID({});
                    }
                }
            }
        }
    }

    // 7. Rebuild sidechain LFO cache
    rebuildSidechainLFOCache();

    DBG("PluginManager::cleanupTrackPlugins - cleaned up track "
        << trackId << " (" << deviceIds.size() << " devices removed)");
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

    // Special cases: custom plugins that need ValueTree state, or helper creators
    if (type.equalsIgnoreCase(daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, daw::audio::MagdaSamplerPlugin::xmlTypeName,
                                nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, daw::audio::DrumGridPlugin::xmlTypeName, nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase(daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, daw::audio::MidiChordEnginePlugin::xmlTypeName,
                                nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, daw::audio::ArpeggiatorPlugin::xmlTypeName, nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase(daw::audio::StepSequencerPlugin::xmlTypeName)) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, daw::audio::StepSequencerPlugin::xmlTypeName,
                                nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (type.equalsIgnoreCase("tone") || type.equalsIgnoreCase("tonegenerator")) {
        plugin = createToneGenerator(track);
    } else if (type.equalsIgnoreCase("meter") || type.equalsIgnoreCase("levelmeter")) {
        plugin = createLevelMeter(track);
    } else {
        // Standard TE built-in plugins: look up xmlTypeName from user-facing name
        static const std::unordered_map<juce::String, juce::String> builtInPluginTypes = {
            {"delay", te::DelayPlugin::xmlTypeName},
            {"reverb", te::ReverbPlugin::xmlTypeName},
            {"eq", te::EqualiserPlugin::xmlTypeName},
            {"equaliser", te::EqualiserPlugin::xmlTypeName},
            {"compressor", te::CompressorPlugin::xmlTypeName},
            {"chorus", te::ChorusPlugin::xmlTypeName},
            {"phaser", te::PhaserPlugin::xmlTypeName},
            {"lowpass", te::LowPassPlugin::xmlTypeName},
            {"pitchshift", te::PitchShiftPlugin::xmlTypeName},
            {"impulseresponse", te::ImpulseResponsePlugin::xmlTypeName},
            {"utility", te::VolumeAndPanPlugin::xmlTypeName},
        };

        auto it = builtInPluginTypes.find(type.toLowerCase());
        if (it != builtInPluginTypes.end()) {
            plugin = edit_.getPluginCache().createNewPlugin(it->second, {});
            if (plugin)
                track->pluginList.insertPlugin(plugin, -1, nullptr);
        }
    }

    if (!plugin) {
        DBG("Failed to load built-in plugin: " << type);
    }

    return plugin;
}

PluginLoadResult PluginManager::loadExternalPlugin(TrackId trackId,
                                                   const juce::PluginDescription& description,
                                                   int insertIndex) {
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
                // (skip this check if the plugin is still loading asynchronously)
                if (!extPlugin->isEnabled() && !extPlugin->isInitialisingAsync()) {
                    juce::String error = "Plugin failed to initialize: " + description.name;
                    if (description.fileOrIdentifier.isNotEmpty()) {
                        error += " (" + description.fileOrIdentifier + ")";
                    }
                    return PluginLoadResult::Failure(error);
                }
            }

            track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
            DBG("Loaded external plugin: " << description.name << " on track " << trackId);
            return PluginLoadResult::Success(plugin);
        } else {
            juce::String error = "Failed to create plugin: " + description.name;
            DBG(error);
            return PluginLoadResult::Failure(error);
        }
    } catch (const std::exception& e) {
        juce::String error = "Exception loading plugin " + description.name + ": " + e.what();
        DBG(error);
        return PluginLoadResult::Failure(error);
    } catch (...) {
        juce::String error = "Unknown exception loading plugin: " + description.name;
        DBG(error);
        return PluginLoadResult::Failure(error);
    }
}

te::Plugin::Ptr PluginManager::addLevelMeterToTrack(TrackId trackId) {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track) {
        DBG("Cannot add LevelMeter: track " << trackId << " not found");
        return nullptr;
    }

    auto& plugins = track->pluginList;

    // Check if a LevelMeterPlugin already exists on this track
    te::LevelMeterPlugin* existingMeter = nullptr;
    int existingIndex = -1;
    int meterCount = 0;
    for (int i = 0; i < plugins.size(); ++i) {
        if (auto* lm = dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
            if (meterCount == 0) {
                existingMeter = lm;
                existingIndex = i;
            }
            ++meterCount;
        }
    }
    // If exactly one LevelMeterPlugin exists and it's already at the end,
    // just ensure the meter client is registered and reuse it.
    if (existingMeter && meterCount == 1 && existingIndex == plugins.size() - 1) {
        trackController_.addMeterClient(trackId, existingMeter);
        return existingMeter;
    }

    // Remove any existing LevelMeter plugins (wrong position or duplicates)
    for (int i = plugins.size() - 1; i >= 0; --i) {
        if (auto* levelMeter = dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
            trackController_.removeMeterClient(trackId);
            levelMeter->deleteFromParent();
        }
    }

    // Add a fresh LevelMeter at the end
    auto plugin = loadBuiltInPlugin(trackId, "levelmeter");

    // Register meter client with the new LevelMeter (thread-safe)
    if (plugin) {
        if (auto* levelMeter = dynamic_cast<te::LevelMeterPlugin*>(plugin.get())) {
            trackController_.addMeterClient(trackId, levelMeter);
        }
    }

    return plugin;
}

void PluginManager::pollAsyncPluginLoad(TrackId trackId, DeviceId deviceId,
                                        te::Plugin::Ptr plugin) {
    auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get());
    if (!extPlugin)
        return;

    // Use a timer to poll until TE's async instantiation completes.
    // The timer runs on the message thread, same as TE's completion callback.
    // Capture a WeakReference to guard against PluginManager destruction.
    juce::WeakReference<PluginManager> weakThis(this);
    juce::Timer::callAfterDelay(100, [weakThis, trackId, deviceId, plugin]() {
        if (weakThis == nullptr)
            return;  // PluginManager was destroyed
        auto& self = *weakThis;

        auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get());
        if (!ext)
            return;

        // Check if device was removed while we were loading
        if (TrackManager::getInstance().getDevice(trackId, deviceId) == nullptr) {
            juce::ScopedLock lock(self.pluginLock_);
            if (auto sdIt = self.syncedDevices_.find(deviceId); sdIt != self.syncedDevices_.end())
                sdIt->second.isPendingLoad = false;
            return;
        }

        if (ext->isInitialisingAsync()) {
            // Still loading — poll again
            self.pollAsyncPluginLoad(trackId, deviceId, plugin);
            return;
        }

        // Loading complete — update state
        {
            juce::ScopedLock lock(self.pluginLock_);
            if (auto sdIt = self.syncedDevices_.find(deviceId); sdIt != self.syncedDevices_.end())
                sdIt->second.isPendingLoad = false;
        }

        bool loaded = ext->getLoadError().isEmpty();
        if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, deviceId)) {
            devInfo->loadState = loaded ? DeviceLoadState::Loaded : DeviceLoadState::Failed;
        }

        if (loaded) {
            // Apply bypass state
            plugin->setEnabled(true);
            if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, deviceId)) {
                plugin->setEnabled(!devInfo->bypassed);
            }

            // Create processor now that the plugin instance is ready
            auto extProcessor = std::make_unique<ExternalPluginProcessor>(deviceId, plugin);
            extProcessor->startParameterListening();

            // Populate parameters on the DeviceInfo
            if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, deviceId)) {
                extProcessor->populateParameters(*devInfo);

                // Update capability flags
                if (plugin->canSidechain())
                    devInfo->canSidechain = true;
                if (plugin->takesMidiInput() && !devInfo->isInstrument)
                    devInfo->canReceiveMidi = true;
            }

            {
                juce::ScopedLock lock(self.pluginLock_);
                self.syncedDevices_[deviceId].processor = std::move(extProcessor);
            }

            // Wrap instruments in a RackType (for audio passthrough + multi-out)
            if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, deviceId)) {
                if (devInfo->isInstrument) {
                    int numOutputChannels = ext->getNumOutputs();

                    // Remember the plugin's position before wrapping removes it
                    auto* track = self.trackController_.getAudioTrack(trackId);
                    int pluginIdx = track ? track->pluginList.indexOf(plugin.get()) : -1;

                    te::Plugin::Ptr rackPlugin;
                    if (numOutputChannels > 2) {
                        rackPlugin = self.instrumentRackManager_.wrapMultiOutInstrument(
                            plugin, numOutputChannels);
                    } else {
                        rackPlugin = self.instrumentRackManager_.wrapInstrument(plugin);
                    }

                    if (rackPlugin) {
                        // Insert the rack instance back at the original position
                        if (track)
                            track->pluginList.insertPlugin(rackPlugin, pluginIdx, nullptr);

                        auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
                        te::RackType::Ptr rackType = rackInstance ? rackInstance->type : nullptr;
                        self.instrumentRackManager_.recordWrapping(
                            deviceId, rackType, plugin, rackPlugin, numOutputChannels > 2,
                            numOutputChannels);
                    }
                }
            }
        }

        // Notify so AudioBridge re-syncs infrastructure and UI rebuilds
        if (self.onAsyncPluginLoaded)
            self.onAsyncPluginLoaded(trackId);
    });
}

void PluginManager::ensureVolumePluginPosition(te::AudioTrack* track) const {
    if (!track)
        return;

    auto& plugins = track->pluginList;

    // Find the track's fader VolumeAndPanPlugin, excluding any Utility instances
    // (which are also VolumeAndPanPlugins but are tracked in syncedDevices_).
    // Snapshot synced plugin pointers under the lock to avoid racing with mutations.
    std::unordered_set<te::Plugin*> syncedPlugins;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [devId, syncInfo] : syncedDevices_) {
            if (syncInfo.plugin)
                syncedPlugins.insert(syncInfo.plugin.get());
        }
    }

    te::VolumeAndPanPlugin* volPanRaw = nullptr;
    int volPanIndex = -1;
    for (int i = 0; i < plugins.size(); ++i) {
        if (auto* vp = dynamic_cast<te::VolumeAndPanPlugin*>(plugins[i])) {
            if (syncedPlugins.find(vp) == syncedPlugins.end()) {
                volPanRaw = vp;
                volPanIndex = i;
            }
        }
    }
    if (!volPanRaw)
        return;

    te::Plugin::Ptr volPanPlugin = volPanRaw;

    if (volPanIndex < 0)
        return;

    // Check if there are any non-utility plugins after VolumeAndPan.
    // VolumeAndPan is the track fader — it must come AFTER all audio-producing
    // plugins (instruments, racks, FX, sends) and only before LevelMeter.
    bool needsMove = false;
    for (int i = volPanIndex + 1; i < plugins.size(); ++i) {
        if (!dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
            needsMove = true;
            break;
        }
    }

    if (!needsMove)
        return;

    // Move VolumeAndPan to the end of the list.
    // addLevelMeterToTrack() runs right after this and ensures LevelMeter
    // is always the very last plugin, so the final order will be:
    // [instruments, FX, sends, ..., VolumeAndPan, LevelMeter]
    volPanPlugin->removeFromParent();
    plugins.insertPlugin(volPanPlugin, -1, nullptr);

    DBG("Moved VolumeAndPanPlugin from position " << volPanIndex << " to end");
}

// =============================================================================
// Multi-Output Track Sync
// =============================================================================

void PluginManager::syncMultiOutTrack(TrackId trackId, const TrackInfo& trackInfo) {
    if (!trackInfo.multiOutLink.has_value())
        return;

    const auto& link = *trackInfo.multiOutLink;

    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (!teTrack) {
        teTrack = trackController_.createAudioTrack(trackId, trackInfo.name);
    }
    if (!teTrack)
        return;

    // Look up the output pair's actual pin mapping
    auto* device = TrackManager::getInstance().getDevice(link.sourceTrackId, link.sourceDeviceId);
    if (!device || !device->multiOut.isMultiOut)
        return;

    if (link.outputPairIndex < 0 ||
        link.outputPairIndex >= static_cast<int>(device->multiOut.outputPairs.size()))
        return;

    auto& outPair = device->multiOut.outputPairs[static_cast<size_t>(link.outputPairIndex)];

    // Restore pair state from the existing multi-out track (needed after project load,
    // since the async plugin callback rebuilds pairs with active=false before tracks are restored)
    if (!outPair.active || outPair.trackId != trackId) {
        outPair.active = true;
        outPair.trackId = trackId;
    }

    // Get or create the RackInstance for this output pair
    auto rackInstance = instrumentRackManager_.createOutputInstance(
        link.sourceDeviceId, link.outputPairIndex, outPair.firstPin, outPair.numChannels);
    if (!rackInstance)
        return;

    // Check if rack instance is already on the track
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

    // Sync user-added FX devices from chainElements (same as normal track path)
    for (size_t elemIdx = 0; elemIdx < trackInfo.chainElements.size(); ++elemIdx) {
        const auto& element = trackInfo.chainElements[elemIdx];
        if (isDevice(element)) {
            const auto& device = getDevice(element);

            juce::ScopedLock lock(pluginLock_);
            if (syncedDevices_.find(device.id) == syncedDevices_.end()) {
                // Compute TE insertion index from subsequent synced devices
                int teInsertIndex = -1;
                for (size_t j = elemIdx + 1; j < trackInfo.chainElements.size(); ++j) {
                    if (isDevice(trackInfo.chainElements[j])) {
                        auto nextId = getDevice(trackInfo.chainElements[j]).id;
                        auto it = syncedDevices_.find(nextId);
                        if (it != syncedDevices_.end() && it->second.plugin) {
                            auto* rackInst = instrumentRackManager_.getRackInstance(nextId);
                            auto* pluginOnTrack = rackInst ? rackInst : it->second.plugin.get();
                            int idx = teTrack->pluginList.indexOf(pluginOnTrack);
                            if (idx >= 0) {
                                teInsertIndex = idx;
                                break;
                            }
                        }
                    }
                }

                auto plugin = loadDeviceAsPlugin(trackId, device, teInsertIndex);
                if (plugin) {
                    syncedDevices_[device.id].trackId = trackId;
                    syncedDevices_[device.id].plugin = plugin;
                    pluginToDevice_[plugin.get()] = device.id;
                }
            }
        }
    }

    // Reorder TE plugins to match the MAGDA chain element order (same as syncTrackPlugins)
    {
        std::vector<te::Plugin*> desiredOrder;
        for (const auto& element : trackInfo.chainElements) {
            if (isDevice(element)) {
                juce::ScopedLock lock(pluginLock_);
                auto it = syncedDevices_.find(getDevice(element).id);
                if (it != syncedDevices_.end() && it->second.plugin) {
                    auto* wrapped = instrumentRackManager_.getRackInstance(it->first);
                    auto* pluginToFind = wrapped ? wrapped : it->second.plugin.get();
                    if (teTrack->pluginList.indexOf(pluginToFind) >= 0)
                        desiredOrder.push_back(pluginToFind);
                }
            }
        }

        auto& listState = teTrack->pluginList.state;
        for (size_t i = 0; i < desiredOrder.size(); ++i) {
            int vtChildIdx = listState.indexOf(desiredOrder[i]->state);
            if (vtChildIdx < 0)
                continue;

            if (i == 0) {
                // First user plugin: move after the multi-out rack instance and
                // any fixed front-of-chain plugins (SidechainMonitorPlugin, AuxReturn).
                int targetVtIdx = 0;
                if (rackInstance) {
                    int rackVtIdx = listState.indexOf(rackInstance->state);
                    if (rackVtIdx >= 0)
                        targetVtIdx = rackVtIdx + 1;
                }
                // Also skip past any fixed front-of-chain plugins
                for (int c = targetVtIdx; c < listState.getNumChildren(); ++c) {
                    auto child = listState.getChild(c);
                    if (child.hasType(te::IDs::PLUGIN)) {
                        auto type = child.getProperty(te::IDs::type).toString();
                        if (type == "auxreturn" || type == SidechainMonitorPlugin::xmlTypeName)
                            targetVtIdx = c + 1;
                        else
                            break;
                    }
                }
                if (vtChildIdx != targetVtIdx)
                    listState.moveChild(vtChildIdx, targetVtIdx, nullptr);
            } else {
                int prevVtIdx = listState.indexOf(desiredOrder[i - 1]->state);
                int curVtIdx = listState.indexOf(desiredOrder[i]->state);
                if (curVtIdx >= 0 && prevVtIdx >= 0 && curVtIdx != prevVtIdx + 1)
                    listState.moveChild(curVtIdx, prevVtIdx + 1, nullptr);
            }
        }
    }

    // Ensure VolumeAndPan and LevelMeter are present
    ensureVolumePluginPosition(teTrack);
    addLevelMeterToTrack(trackId);

    // Set audio output routing (e.g. "track:N" to route back to parent)
    if (trackInfo.audioOutputDevice.isNotEmpty())
        trackController_.setTrackAudioOutput(trackId, trackInfo.audioOutputDevice);

    DBG("syncMultiOutTrack: trackId=" << trackId << " pair=" << link.outputPairIndex
                                      << " firstPin=" << outPair.firstPin);
}

// =============================================================================
// Master Channel Plugin Sync
// =============================================================================

void PluginManager::syncMasterPlugins() {
    auto* trackInfo = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
    if (!trackInfo)
        return;

    auto& masterList = edit_.getMasterPluginList();

    // Collect current MAGDA device IDs on master
    std::vector<DeviceId> magdaDevices;
    for (const auto& element : trackInfo->chainElements) {
        if (isDevice(element))
            magdaDevices.push_back(getDevice(element).id);
    }

    // Remove synced plugins that are no longer in MAGDA's master chain
    std::vector<DeviceId> toRemove;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (!sd.plugin)
                continue;
            // Check if plugin belongs to master plugin list
            bool belongsToMaster = false;
            for (int i = 0; i < masterList.size(); ++i) {
                if (masterList[i] == sd.plugin.get()) {
                    belongsToMaster = true;
                    break;
                }
            }
            if (belongsToMaster) {
                bool found = std::find(magdaDevices.begin(), magdaDevices.end(), deviceId) !=
                             magdaDevices.end();
                if (!found) {
                    toRemove.push_back(deviceId);
                    pluginsToDelete.push_back(sd.plugin);
                }
            }
        }
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (auto deviceId : toRemove) {
            auto it = syncedDevices_.find(deviceId);
            if (it != syncedDevices_.end()) {
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get()))
                    dg->removeListener(this);
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                syncedDevices_.erase(it);
            }
        }
    }
    for (auto& plugin : pluginsToDelete) {
        plugin->deleteFromParent();
    }

    // Add new plugins for MAGDA devices not yet synced
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;
        const auto& device = getDevice(element);
        {
            juce::ScopedLock lock(pluginLock_);
            if (syncedDevices_.find(device.id) != syncedDevices_.end())
                continue;
        }

        auto plugin = createPluginOnly(MASTER_TRACK_ID, device);
        if (!plugin)
            continue;

        masterList.insertPlugin(plugin, -1, nullptr);
        {
            juce::ScopedLock lock(pluginLock_);
            syncedDevices_[device.id].trackId = MASTER_TRACK_ID;
            syncedDevices_[device.id].plugin = plugin;
            pluginToDevice_[plugin.get()] = device.id;
        }

        // Create processor so UI parameter changes reach the TE plugin
        registerRackPluginProcessor(device.id, plugin, device);

        // Update capability flags on the DeviceInfo
        if (auto* devInfo = TrackManager::getInstance().getDevice(MASTER_TRACK_ID, device.id)) {
            if (plugin->canSidechain())
                devInfo->canSidechain = true;
            if (plugin->takesMidiInput() && !device.isInstrument)
                devInfo->canReceiveMidi = true;
        }

        // Handle async loading for external plugins
        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            if (extPlugin->isInitialisingAsync()) {
                juce::ScopedLock lock(pluginLock_);
                syncedDevices_[device.id].isPendingLoad = true;
                if (auto* devInfo =
                        TrackManager::getInstance().getDevice(MASTER_TRACK_ID, device.id)) {
                    devInfo->loadState = DeviceLoadState::Loading;
                }
                TrackManager::getInstance().notifyTrackDevicesChanged(MASTER_TRACK_ID);
                pollAsyncPluginLoad(MASTER_TRACK_ID, device.id, plugin);
            }
        }
    }

    DBG("syncMasterPlugins: synced " << magdaDevices.size() << " devices on master");
}

// =============================================================================
// Rack Plugin Creation
// =============================================================================

te::Plugin::Ptr PluginManager::createPluginOnly(TrackId trackId, const DeviceInfo& device) {
    te::Plugin::Ptr plugin;

    if (device.format == PluginFormat::Internal) {
        const auto& ps = device.pluginState;
        if (device.pluginId.containsIgnoreCase("delay")) {
            plugin = createInternalPlugin(te::DelayPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("reverb")) {
            plugin = createInternalPlugin(te::ReverbPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("eq")) {
            plugin = createInternalPlugin(te::EqualiserPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("compressor")) {
            plugin = createInternalPlugin(te::CompressorPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("chorus")) {
            plugin = createInternalPlugin(te::ChorusPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("phaser")) {
            plugin = createInternalPlugin(te::PhaserPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("lowpass")) {
            plugin = createInternalPlugin(te::LowPassPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("pitchshift")) {
            plugin = createInternalPlugin(te::PitchShiftPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("impulseresponse")) {
            plugin = createInternalPlugin(te::ImpulseResponsePlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("tone")) {
            plugin = createInternalPlugin(te::ToneGeneratorPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("4osc")) {
            plugin = createInternalPlugin(te::FourOscPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase("utility") ||
                   device.pluginId.containsIgnoreCase("volume")) {
            plugin = createInternalPlugin(te::VolumeAndPanPlugin::xmlTypeName, ps);
        } else if (device.pluginId.containsIgnoreCase(
                       daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
            juce::ValueTree ps(te::IDs::PLUGIN);
            ps.setProperty(te::IDs::type, daw::audio::MagdaSamplerPlugin::xmlTypeName, nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(ps);
        } else if (device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
            juce::ValueTree ps(te::IDs::PLUGIN);
            ps.setProperty(te::IDs::type, daw::audio::DrumGridPlugin::xmlTypeName, nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(ps);

            // Restore DrumGridPlugin chain state from saved XML
            if (plugin && device.pluginState.isNotEmpty()) {
                if (auto xml = juce::XmlDocument::parse(device.pluginState)) {
                    auto savedState = juce::ValueTree::fromXml(*xml);
                    if (savedState.isValid()) {
                        plugin->restorePluginStateFromValueTree(savedState);
                    }
                }
            }
        } else if (device.pluginId.containsIgnoreCase(
                       daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
            juce::ValueTree ps(te::IDs::PLUGIN);
            ps.setProperty(te::IDs::type, daw::audio::MidiChordEnginePlugin::xmlTypeName, nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(ps);
        } else if (device.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
            juce::ValueTree ps(te::IDs::PLUGIN);
            ps.setProperty(te::IDs::type, daw::audio::ArpeggiatorPlugin::xmlTypeName, nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(ps);
        } else if (device.pluginId.containsIgnoreCase(
                       daw::audio::StepSequencerPlugin::xmlTypeName)) {
            juce::ValueTree ps(te::IDs::PLUGIN);
            ps.setProperty(te::IDs::type, daw::audio::StepSequencerPlugin::xmlTypeName, nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(ps);
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

            // Restore plugin native state for rack plugins
            if (plugin && device.pluginState.isNotEmpty()) {
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                    ext->state.setProperty(te::IDs::state, device.pluginState, nullptr);
                    if (!ext->isInitialisingAsync()) {
                        ext->restorePluginStateFromValueTree(ext->state);
                    }
                }
            }
        }
    }

    if (plugin) {
        plugin->setEnabled(!device.bypassed);
    }

    return plugin;
}

// =============================================================================
// Rack Plugin Processor Registration
// =============================================================================

void PluginManager::registerRackPluginProcessor(DeviceId deviceId, te::Plugin::Ptr plugin,
                                                const DeviceInfo& device) {
    if (!plugin)
        return;

    std::unique_ptr<DeviceProcessor> processor;

    if (dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        auto extProc = std::make_unique<ExternalPluginProcessor>(deviceId, plugin);
        extProc->startParameterListening();

        // Populate parameters back to TrackManager
        DeviceInfo tempInfo;
        extProc->populateParameters(tempInfo);
        TrackManager::getInstance().updateDeviceParameters(deviceId, tempInfo.parameters);
        AutoAliasGenerator::regenerateForDevice(deviceId);

        processor = std::move(extProc);
    } else if (dynamic_cast<te::FourOscPlugin*>(plugin.get())) {
        processor = std::make_unique<FourOscProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::DelayPlugin*>(plugin.get())) {
        processor = std::make_unique<DelayProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::ReverbPlugin*>(plugin.get())) {
        processor = std::make_unique<ReverbProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::EqualiserPlugin*>(plugin.get())) {
        processor = std::make_unique<EqualiserProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::CompressorPlugin*>(plugin.get())) {
        processor = std::make_unique<CompressorProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::ChorusPlugin*>(plugin.get())) {
        processor = std::make_unique<ChorusProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::PhaserPlugin*>(plugin.get())) {
        processor = std::make_unique<PhaserProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::LowPassPlugin*>(plugin.get())) {
        processor = std::make_unique<FilterProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::PitchShiftPlugin*>(plugin.get())) {
        processor = std::make_unique<PitchShiftProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::ImpulseResponsePlugin*>(plugin.get())) {
        processor = std::make_unique<ImpulseResponseProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::ToneGeneratorPlugin*>(plugin.get())) {
        processor = std::make_unique<ToneGeneratorProcessor>(deviceId, plugin);
    } else if (dynamic_cast<te::VolumeAndPanPlugin*>(plugin.get())) {
        processor = std::make_unique<UtilityProcessor>(deviceId, plugin);
    } else if (dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin.get())) {
        processor = std::make_unique<MagdaSamplerProcessor>(deviceId, plugin);
    } else if (dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
        processor = std::make_unique<DrumGridProcessor>(deviceId, plugin);
    }

    if (processor) {
        // Restore parameter values from DeviceInfo onto the newly created plugin
        processor->syncFromDeviceInfo(device);

        // Populate parameters back to TrackManager so the DeviceInfo has parameter metadata
        // (needed for UI controls to function — setDeviceParameterValue checks params.size())
        DeviceInfo tempInfo;
        processor->populateParameters(tempInfo);
        TrackManager::getInstance().updateDeviceParameters(deviceId, tempInfo.parameters);
        AutoAliasGenerator::regenerateForDevice(deviceId);

        juce::ScopedLock lock(pluginLock_);
        syncedDevices_[deviceId].processor = std::move(processor);
        DBG("PluginManager::registerRackPluginProcessor: Registered processor for device "
            << deviceId);
    }
}

// =============================================================================
// Internal Implementation
// =============================================================================

te::Plugin::Ptr PluginManager::loadDeviceAsPlugin(TrackId trackId, const DeviceInfo& device,
                                                  int insertIndex) {
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
        } else if (device.pluginId.containsIgnoreCase(
                       daw::audio::MagdaSamplerPlugin::xmlTypeName)) {
            juce::ValueTree pluginState(te::IDs::PLUGIN);
            pluginState.setProperty(te::IDs::type, daw::audio::MagdaSamplerPlugin::xmlTypeName,
                                    nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<MagdaSamplerProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName)) {
            juce::ValueTree pluginState(te::IDs::PLUGIN);
            pluginState.setProperty(te::IDs::type, daw::audio::DrumGridPlugin::xmlTypeName,
                                    nullptr);
            plugin = edit_.getPluginCache().createNewPlugin(pluginState);
            if (plugin) {
                // Don't restore state here — defer until after rack wrapping.
                // Restoring adds PLUGIN children (samplers) to DrumGrid's state,
                // which can confuse TE's rack graph builder.
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<DrumGridProcessor>(device.id, plugin);

                // Register as listener for auto multi-out track sync
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get()))
                    dg->addListener(this);
            }
        } else if (device.pluginId.containsIgnoreCase("4osc")) {
            plugin = createInternalPlugin(te::FourOscPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<FourOscProcessor>(device.id, plugin);
            }
            // Note: "volume" devices are NOT created here - track volume is separate infrastructure
            // managed by ensureVolumePluginPosition() and controlled via
            // TrackManager::setTrackVolume()
        } else if (device.pluginId.containsIgnoreCase("meter")) {
            plugin = createLevelMeter(track);
            // No processor for meter - it's just for measurement
        } else if (device.pluginId.containsIgnoreCase(
                       daw::audio::MidiChordEnginePlugin::xmlTypeName)) {
            plugin = createInternalPlugin(daw::audio::MidiChordEnginePlugin::xmlTypeName,
                                          device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                // No processor — analysis-only plugin with transparent passthrough
            }
        } else if (device.pluginId.containsIgnoreCase(daw::audio::ArpeggiatorPlugin::xmlTypeName)) {
            plugin = createInternalPlugin(daw::audio::ArpeggiatorPlugin::xmlTypeName,
                                          device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<ArpeggiatorProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase(
                       daw::audio::StepSequencerPlugin::xmlTypeName)) {
            plugin = createInternalPlugin(daw::audio::StepSequencerPlugin::xmlTypeName,
                                          device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<StepSequencerProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("delay")) {
            plugin = createInternalPlugin(te::DelayPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<DelayProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("reverb")) {
            plugin = createInternalPlugin(te::ReverbPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<ReverbProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("eq")) {
            plugin = createInternalPlugin(te::EqualiserPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<EqualiserProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("compressor")) {
            plugin = createInternalPlugin(te::CompressorPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<CompressorProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("chorus")) {
            plugin = createInternalPlugin(te::ChorusPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<ChorusProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("phaser")) {
            plugin = createInternalPlugin(te::PhaserPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<PhaserProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("lowpass")) {
            plugin = createInternalPlugin(te::LowPassPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<FilterProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("pitchshift")) {
            plugin = createInternalPlugin(te::PitchShiftPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<PitchShiftProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("impulseresponse")) {
            plugin =
                createInternalPlugin(te::ImpulseResponsePlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<ImpulseResponseProcessor>(device.id, plugin);
            }
        } else if (device.pluginId.containsIgnoreCase("utility")) {
            plugin = createInternalPlugin(te::VolumeAndPanPlugin::xmlTypeName, device.pluginState);
            if (plugin) {
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
                processor = std::make_unique<UtilityProcessor>(device.id, plugin);
            }
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

            auto result = loadExternalPlugin(trackId, desc, insertIndex);
            if (result.success && result.plugin) {
                plugin = result.plugin;

                // Restore plugin native state (base64 blob) from DeviceInfo
                // For async plugins, TE reads the state property during init.
                // For sync plugins, we also call restorePluginStateFromValueTree().
                restorePluginState(trackId, device.id, plugin);

                // If the plugin is loading asynchronously (TE background thread),
                // skip processor creation — it will be done in pollAsyncPluginLoad
                // when the VST instance is ready.
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                    if (ext->isInitialisingAsync()) {
                        return plugin;  // Return bare wrapper; async poll handles the rest
                    }
                    // Sync plugin already created — re-apply state now
                    if (device.pluginState.isNotEmpty()) {
                        ext->restorePluginStateFromValueTree(ext->state);
                    }
                }

                auto extProcessor = std::make_unique<ExternalPluginProcessor>(device.id, plugin);
                // Start listening for parameter changes from the plugin's native UI
                extProcessor->startParameterListening();
                processor = std::move(extProcessor);
            } else {
                // Plugin failed to load - notify via callback
                if (onPluginLoadFailed) {
                    onPluginLoadFailed(device.id, result.errorMessage);
                }
                DBG("Plugin load failed for device " << device.id << ": " << result.errorMessage);
                return nullptr;  // Don't proceed with a failed plugin
            }
        } else {
            DBG("Cannot load external plugin without uniqueId or fileOrIdentifier: "
                << device.name);
        }
    }

    if (plugin) {
        // Update capability flags on the DeviceInfo in TrackManager
        if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, device.id)) {
            if (plugin->canSidechain())
                devInfo->canSidechain = true;
            if (plugin->takesMidiInput() && !device.isInstrument)
                devInfo->canReceiveMidi = true;
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
            AutoAliasGenerator::regenerateForDevice(device.id);

            syncedDevices_[device.id].processor = std::move(processor);
        }

        // Apply device state
        plugin->setEnabled(!device.bypassed);

        // Wrap instruments in a RackType with audio passthrough so both synth
        // output and audio clips on the same track are summed together.
        if (device.isInstrument) {
            // Detect multi-output capability
            int numOutputChannels = 2;
            if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                numOutputChannels = extPlugin->getNumOutputs();
            } else if (auto* drumGrid = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
                numOutputChannels = drumGrid->getNumOutputChannels();
            }

            // Remember the plugin's position before wrapping removes it from the track
            int pluginIdx = track->pluginList.indexOf(plugin.get());

            te::Plugin::Ptr rackPlugin;
            if (numOutputChannels > 2) {
                rackPlugin =
                    instrumentRackManager_.wrapMultiOutInstrument(plugin, numOutputChannels);
            } else {
                rackPlugin = instrumentRackManager_.wrapInstrument(plugin);
            }

            if (rackPlugin) {
                // Insert the rack instance back on the track at the original position
                track->pluginList.insertPlugin(rackPlugin, pluginIdx, nullptr);

                // Record the wrapping so we can look up the inner plugin later
                auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
                te::RackType::Ptr rackType = rackInstance ? rackInstance->type : nullptr;
                instrumentRackManager_.recordWrapping(device.id, rackType, plugin, rackPlugin,
                                                      numOutputChannels > 2, numOutputChannels);

                // Populate multi-out config on the DeviceInfo
                if (numOutputChannels > 2) {
                    // Populate MultiOutConfig on the DeviceInfo
                    if (auto* devInfo = TrackManager::getInstance().getDevice(trackId, device.id)) {
                        devInfo->multiOut.isMultiOut = true;
                        devInfo->multiOut.totalOutputChannels = numOutputChannels;
                        devInfo->multiOut.outputPairs.clear();

                        // Build output pair names from plugin's output buses
                        // Each bus typically represents a stereo pair with a meaningful name
                        juce::AudioPluginInstance* pi = nullptr;
                        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                            pi = extPlugin->getAudioPluginInstance();
                        }

                        int pairIndex = 0;
                        int pinOffset = 1;  // 1-based rack output pin index
                        if (pi != nullptr) {
                            int numBuses = pi->getBusCount(false);
                            for (int b = 0; b < numBuses; ++b) {
                                if (auto* bus = pi->getBus(false, b)) {
                                    int busChannels = bus->getNumberOfChannels();
                                    int busPairs = std::max(1, busChannels / 2);
                                    juce::String busName = bus->getName();
                                    int channelsPerPair = std::max(1, busChannels / busPairs);

                                    for (int bp = 0; bp < busPairs; ++bp) {
                                        MultiOutOutputPair pair;
                                        pair.outputIndex = pairIndex;
                                        pair.firstPin = pinOffset;
                                        pair.numChannels = channelsPerPair;
                                        if (busPairs == 1) {
                                            pair.name = busName;
                                        } else {
                                            pair.name = busName + " " + juce::String(bp + 1);
                                        }
                                        devInfo->multiOut.outputPairs.push_back(pair);
                                        pinOffset += channelsPerPair;
                                        ++pairIndex;
                                    }
                                }
                            }
                        }

                        // DrumGrid-specific bus names
                        if (devInfo->multiOut.outputPairs.empty() &&
                            dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
                            int numPairs = numOutputChannels / 2;
                            for (int p = 0; p < numPairs; ++p) {
                                MultiOutOutputPair pair;
                                pair.outputIndex = p;
                                pair.firstPin = p * 2 + 1;
                                pair.numChannels = 2;
                                pair.name = (p == 0) ? "Main" : ("Bus " + juce::String(p));
                                devInfo->multiOut.outputPairs.push_back(pair);
                            }
                        }

                        // Fallback: if no buses found, generate generic names
                        if (devInfo->multiOut.outputPairs.empty()) {
                            int numPairs = numOutputChannels / 2;
                            for (int p = 0; p < numPairs; ++p) {
                                MultiOutOutputPair pair;
                                pair.outputIndex = p;
                                pair.firstPin = p * 2 + 1;
                                pair.numChannels = 2;
                                pair.name = "Out " + juce::String(p * 2 + 1) + "-" +
                                            juce::String(p * 2 + 2);
                                devInfo->multiOut.outputPairs.push_back(pair);
                            }
                        }

                        DBG("PluginManager: Detected multi-out instrument with "
                            << numOutputChannels << " outputs ("
                            << devInfo->multiOut.outputPairs.size() << " stereo pairs)");
                    }
                }

                DBG("Loaded instrument device " << device.id << " (" << device.name
                                                << ") wrapped in rack");

                // Deferred restore: restore DrumGrid chain state AFTER wrapping,
                // so nested PLUGIN children don't confuse TE's rack graph builder.
                if (device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName) &&
                    device.pluginState.isNotEmpty()) {
                    if (auto xml = juce::XmlDocument::parse(device.pluginState)) {
                        auto savedState = juce::ValueTree::fromXml(*xml);
                        if (savedState.isValid()) {
                            plugin->restorePluginStateFromValueTree(savedState);
                        }
                    }
                }

                // Also restore standalone sampler state after wrapping
                if (device.pluginId.containsIgnoreCase(
                        daw::audio::MagdaSamplerPlugin::xmlTypeName) &&
                    device.pluginState.isNotEmpty()) {
                    if (auto xml = juce::XmlDocument::parse(device.pluginState)) {
                        auto savedState = juce::ValueTree::fromXml(*xml);
                        if (savedState.isValid()) {
                            plugin->restorePluginStateFromValueTree(savedState);
                        }
                    }
                }

                // Create a TE FolderTrack (submix) for DrumGrid so the parent and
                // all multi-out children are summed under one fader — like
                // Return the INNER plugin (not the rack) so that syncedDevices_
                // maps to the actual synth for parameter access and window opening
                return plugin;
            }
            // Fallback: if wrapping failed, the plugin was already removed from the
            // track by wrapInstrument, so re-insert it directly
            track->pluginList.insertPlugin(plugin, -1, nullptr);
            DBG("InstrumentRackManager: Wrapping failed for " << device.name
                                                              << ", using raw plugin");
        }

        // For tone generators (always transport-synced), sync initial state with transport
        if (auto* toneProc = syncedDevices_[device.id].processor.get()) {
            if (auto* toneGen = dynamic_cast<ToneGeneratorProcessor*>(toneProc)) {
                // Get current transport state
                bool isPlaying = transportState_.isPlaying();
                // Bypass if transport is not playing
                toneGen->setBypassed(!isPlaying);
            }
        }

        DBG("Loaded device " << device.id << " (" << device.name << ") as plugin");

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

te::Plugin::Ptr PluginManager::createInternalPlugin(const juce::String& xmlTypeName,
                                                    const juce::String& savedPluginState) {
    DBG("createInternalPlugin: type=" << xmlTypeName.toRawUTF8()
                                      << " hasState=" << (int)savedPluginState.isNotEmpty()
                                      << " stateLen=" << savedPluginState.length());

    if (savedPluginState.isNotEmpty()) {
        if (auto xml = juce::parseXML(savedPluginState)) {
            auto savedState = juce::ValueTree::fromXml(*xml);
            DBG("createInternalPlugin: parsed XML ok, VT type="
                << savedState.getType().toString().toRawUTF8()
                << " hasId=" << (int)savedState.hasProperty(te::IDs::id)
                << " id=" << savedState.getProperty(te::IDs::id).toString().toRawUTF8()
                << " numProps=" << savedState.getNumProperties()
                << " numChildren=" << savedState.getNumChildren());
            if (savedState.isValid()) {
                stripTracktionIdsRecursive(savedState);
                auto plugin = edit_.getPluginCache().createNewPlugin(savedState);
                DBG("createInternalPlugin: from saved state -> plugin="
                    << (plugin ? plugin->getName().toRawUTF8() : "NULL")
                    << " itemID=" << (plugin ? (juce::int64)plugin->itemID.getRawID() : -1));
                return plugin;
            }
        } else {
            DBG("createInternalPlugin: WARNING - failed to parse XML from saved state");
        }
    }

    // Try the string overload first (works for built-in TE plugins like delay, reverb, etc.)
    auto plugin = edit_.getPluginCache().createNewPlugin(xmlTypeName, {});

    // For custom plugins (chord engine, arpeggiator, step sequencer, etc.) the string overload
    // doesn't work — TE only routes the ValueTree overload through createCustomPlugin.
    if (!plugin) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, xmlTypeName, nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    }

    // Override TE's default band freqs on a fresh EQ — the stock values cluster
    // the four bands close together, so the curve looks like one bump instead
    // of a useful low/low-mid/high-mid/high spread. Log-spaced defaults match
    // the colour layout in EqualiserUI (Low / Mid 1 / Mid 2 / High).
    // Values are raw Hz; route through setParameterFromHost like every other
    // host-side write so the path is uniform across the codebase.
    if (plugin && xmlTypeName == te::EqualiserPlugin::xmlTypeName) {
        auto params = plugin->getAutomatableParameters();
        const float defaultFreqs[4] = {100.0f, 500.0f, 3000.0f, 10000.0f};
        for (int band = 0; band < 4; ++band) {
            int freqIndex = band * 3;  // each band lays out as freq/gain/Q
            if (freqIndex < params.size() && params[freqIndex])
                params[freqIndex]->setParameterFromHost(defaultFreqs[band],
                                                        juce::sendNotificationSync);
        }
    }

    DBG("createInternalPlugin: fresh plugin -> "
        << (plugin ? plugin->getName().toRawUTF8() : "NULL")
        << " itemID=" << (plugin ? (juce::int64)plugin->itemID.getRawID() : -1));
    return plugin;
}

//==============================================================================
// DrumGrid multi-out track sync
//==============================================================================

void PluginManager::drumGridChainsChanged(daw::audio::DrumGridPlugin* plugin) {
    if (!plugin)
        return;

    // Hold a ref-counted pointer to keep the plugin alive across the async call.
    te::Plugin::Ptr pluginRef(plugin);

    // Dispatch asynchronously — this callback fires during loadSampleToPad/addChain,
    // and synchronous track activation would re-entrantly destroy UI components
    // (e.g., DeviceSlotComponent) while their callbacks are still on the stack.
    juce::WeakReference<PluginManager> weakThis(this);

    juce::MessageManager::callAsync([weakThis, pluginRef]() {
        auto* self = weakThis.get();
        if (!self)
            return;

        auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(pluginRef.get());
        if (!dg)
            return;

        // Look up the device under lock, then release lock before mutating TrackManager
        // to avoid deadlock (syncDrumGridMultiOutTracks -> TrackManager listeners ->
        // syncAllPlugins -> pluginLock_).
        TrackId matchedTrackId{};
        DeviceId matchedDeviceId{};
        bool foundMatch = false;

        {
            juce::ScopedLock lock(self->pluginLock_);
            for (const auto& [deviceId, synced] : self->syncedDevices_) {
                if (synced.plugin.get() == dg ||
                    self->instrumentRackManager_.getInnerPlugin(deviceId) == dg) {
                    matchedTrackId = synced.trackId;
                    matchedDeviceId = deviceId;
                    foundMatch = true;
                    break;
                }
            }
        }

        if (foundMatch) {
            self->syncDrumGridPadPlugins(matchedTrackId, matchedDeviceId, dg);
            self->syncDrumGridMultiOutTracks(matchedTrackId, matchedDeviceId, dg);
        }
    });
}

void PluginManager::syncDrumGridPadPlugins(TrackId trackId, DeviceId drumGridDeviceId,
                                           daw::audio::DrumGridPlugin* drumGrid) {
    if (!drumGrid)
        return;

    // Collect current valid pad plugin DeviceIds
    std::set<DeviceId> currentIds;
    for (const auto& chain : drumGrid->getChains()) {
        for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
            int devId = drumGrid->getPluginDeviceId(chain->index, pi);
            if (devId >= 0) {
                currentIds.insert(devId);
            }
        }
    }

    juce::ScopedLock lock(pluginLock_);

    // Remove stale entries
    auto& oldIds = drumGridPadDevices_[drumGridDeviceId];
    for (auto oldId : oldIds) {
        if (currentIds.find(oldId) == currentIds.end()) {
            auto it = syncedDevices_.find(oldId);
            if (it != syncedDevices_.end()) {
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                syncedDevices_.erase(it);
            }
        }
    }

    // Add new entries
    for (const auto& chain : drumGrid->getChains()) {
        for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
            int devId = drumGrid->getPluginDeviceId(chain->index, pi);
            if (devId < 0)
                continue;
            if (syncedDevices_.find(devId) == syncedDevices_.end()) {
                auto& sd = syncedDevices_[devId];
                sd.trackId = trackId;
                sd.plugin = chain->plugins[static_cast<size_t>(pi)];
                pluginToDevice_[sd.plugin.get()] = devId;
            }
        }
    }

    oldIds = currentIds;
}

void PluginManager::syncDrumGridMultiOutTracks(TrackId trackId, DeviceId deviceId,
                                               daw::audio::DrumGridPlugin* drumGrid) {
    auto& tm = TrackManager::getInstance();
    auto* devInfo = tm.getDevice(trackId, deviceId);
    if (!devInfo || !devInfo->multiOut.isMultiOut)
        return;

    auto& pairs = devInfo->multiOut.outputPairs;
    const auto& chains = drumGrid->getChains();

    // Build set of bus indices that should be active (non-empty chains with busOutput > 0)
    std::set<int> activeBuses;
    std::map<int, juce::String> busNames;  // bus index → chain name
    for (const auto& chain : chains) {
        int bus = chain->busOutput.get();
        if (bus > 0 && !chain->plugins.empty()) {
            activeBuses.insert(bus);
            busNames[bus] =
                chain->name.isNotEmpty() ? chain->name : ("Pad " + juce::String(chain->index));
        }
    }

    // Deactivate pairs that no longer have a corresponding chain
    for (int p = 1; p < static_cast<int>(pairs.size()); ++p) {
        if (pairs[static_cast<size_t>(p)].active && activeBuses.find(p) == activeBuses.end()) {
            tm.deactivateMultiOutPair(trackId, deviceId, p);
        }
    }

    // Activate pairs for chains that need them
    for (int bus : activeBuses) {
        if (bus >= static_cast<int>(pairs.size()))
            continue;

        auto& pair = pairs[static_cast<size_t>(bus)];
        if (!pair.active) {
            auto childTrackId = tm.activateMultiOutPair(trackId, deviceId, bus);

            if (childTrackId != INVALID_TRACK_ID) {
                // Name the child track after the chain (via setTrackName to notify UI)
                auto it = busNames.find(bus);
                if (it != busNames.end()) {
                    tm.setTrackName(childTrackId, drumGrid->getName() + ": " + it->second);
                    pair.name = it->second;
                }

                // Create the TE audio track and add the RackInstance
                // so audio actually flows through this child track
                if (auto* childTrack = tm.getTrack(childTrackId))
                    syncMultiOutTrack(childTrackId, *childTrack);
            }
        } else if (pair.trackId != INVALID_TRACK_ID) {
            // Update name if chain name changed
            auto it = busNames.find(bus);
            if (it != busNames.end()) {
                auto newName = drumGrid->getName() + ": " + it->second;
                if (auto* childTrack = tm.getTrack(pair.trackId)) {
                    if (childTrack->name != newName) {
                        tm.setTrackName(pair.trackId, newName);
                        pair.name = it->second;
                    }
                }
            }
        }
    }
}

}  // namespace magda
