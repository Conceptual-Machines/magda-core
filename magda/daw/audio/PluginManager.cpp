#include "PluginManager.hpp"

#include <set>
#include <unordered_set>
#include <vector>

#include "../core/RackInfo.hpp"
#include "../core/TrackManager.hpp"
#include "../profiling/PerformanceProfiler.hpp"
#include "ArpeggiatorPlugin.hpp"
#include "CurveSnapshot.hpp"
#include "DrumGridPlugin.hpp"
#include "MagdaSamplerPlugin.hpp"
#include "MidiChordEnginePlugin.hpp"
#include "MidiReceivePlugin.hpp"
#include "ModifierHelpers.hpp"
#include "PluginWindowBridge.hpp"
#include "SidechainMonitorPlugin.hpp"
#include "StepSequencerPlugin.hpp"
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
    auto it = syncedDevices_.find(deviceId);
    if (it != syncedDevices_.end() && it->second.plugin)
        return it->second.plugin;

    // Fall through to rack sync manager for plugins inside racks
    auto* innerPlugin = rackSyncManager_.getInnerPlugin(deviceId);
    if (innerPlugin)
        return innerPlugin;

    return nullptr;
}

DeviceProcessor* PluginManager::getDeviceProcessor(DeviceId deviceId) const {
    juce::ScopedLock lock(pluginLock_);
    auto it = syncedDevices_.find(deviceId);
    return it != syncedDevices_.end() ? it->second.processor.get() : nullptr;
}

DeviceId PluginManager::getDeviceIdForPlugin(te::Plugin* plugin) const {
    if (!plugin)
        return INVALID_DEVICE_ID;

    juce::ScopedLock lock(pluginLock_);
    auto it = pluginToDevice_.find(plugin);
    if (it != pluginToDevice_.end())
        return it->second;

    // Check if this is an instrument wrapper rack
    return instrumentRackManager_.getDeviceIdForRack(plugin);
}

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

    // Sync device-level modifiers (LFOs, etc. assigned to plugin parameters)
    syncDeviceModifiers(trackId, teTrack);

    // Sync device-level macros (macro knobs assigned to plugin parameters)
    syncDeviceMacros(trackId, teTrack);

    // Update mod link fingerprint so resyncDeviceModifiers doesn't rebuild immediately after
    if (auto* info = TrackManager::getInstance().getTrack(trackId))
        modLinkFingerprints_[trackId] = computeModLinkFingerprint(trackId, info);

    // Sync sidechain routing for plugins that support it
    syncSidechains(trackId, teTrack);

    // Sidechain monitor: insert on tracks that need audio-thread MIDI detection
    if (trackNeedsSidechainMonitor(trackId))
        ensureSidechainMonitor(trackId);
    else
        removeSidechainMonitor(trackId);

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

    // Debug: dump final plugin list
    {
        auto& plugins = teTrack->pluginList;
        DBG("syncTrackPlugins: FINAL plugin list for track " << trackId << " (" << plugins.size()
                                                             << " plugins):");
        for (int i = 0; i < plugins.size(); ++i) {
            DBG("  [" << i << "] " << plugins[i]->getName().toRawUTF8()
                      << " enabled=" << (int)plugins[i]->isEnabled()
                      << " itemID=" << (juce::int64)plugins[i]->itemID.getRawID());
        }
    }

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
            DBG("addLevelMeterToTrack: registered meter client for track "
                << trackId << " itemID=" << (juce::int64)plugin->itemID.getRawID());
        }
    } else {
        DBG("addLevelMeterToTrack: WARNING - failed to create LevelMeter for track " << trackId);
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
// Device-Level Modifier Sync
// =============================================================================

void PluginManager::updateDeviceModifierProperties(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    // Update properties on existing TE modifiers without removing/recreating them
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);
        auto sdIt = syncedDevices_.find(device.id);
        if (sdIt == syncedDevices_.end())
            continue;

        auto& existingMods = sdIt->second.modifiers;
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
                // Ensure CurveSnapshotHolder exists for this mod
                auto& snapHolder = sdIt->second.curveSnapshots[modInfo.id];
                if (!snapHolder)
                    snapHolder = std::make_unique<CurveSnapshotHolder>();

                applyLFOProperties(lfo, modInfo, snapHolder.get());
                // Note-triggered LFO retrigger is handled on the audio thread
                // by SidechainMonitorPlugin → triggerSidechainNoteOn.
                // Do NOT call triggerLFONoteOnWithReset here — the message thread
                // runs asynchronously and would reset the ramp a few ms after the
                // audio-thread trigger, shifting the LFO phase and causing the
                // rendered output to differ from playback.
            }

            // Update assignment values (mod depth) for each link
            for (const auto& link : modInfo.links) {
                if (!link.isValid())
                    continue;

                te::Plugin::Ptr linkTarget;
                if (link.target.deviceId == device.id) {
                    juce::ScopedLock lock(pluginLock_);
                    linkTarget = sdIt->second.plugin;
                    if (!linkTarget && device.isInstrument)
                        if (auto* inner = instrumentRackManager_.getInnerPlugin(device.id))
                            linkTarget = inner;
                } else {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = syncedDevices_.find(link.target.deviceId);
                    if (pit != syncedDevices_.end())
                        linkTarget = pit->second.plugin;
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
                                float effectiveAmount = link.amount;
                                if (!renderingActive_ &&
                                    modInfo.triggerMode != LFOTriggerMode::Free &&
                                    !modInfo.running) {
                                    // Gate stopped LFOs, but let completed 1-shot hold
                                    // at end value (phase clamped at 1.0)
                                    if (!modInfo.oneShot || modInfo.phase < 1.0f)
                                        effectiveAmount = 0.0f;
                                }
                                assignment->value = effectiveAmount;
                                assignment->offset = 0.0f;
                                break;
                            }
                        }
                    }
                }
            }

            ++modIdx;
        }
    }

    // Update track-level mod properties in-place
    auto tmIt = trackModStates_.find(trackId);
    if (tmIt != trackModStates_.end()) {
        auto& trackModState = tmIt->second;
        size_t modIdx = 0;
        for (const auto& modInfo : trackInfo->mods) {
            if (!modInfo.enabled || modInfo.links.empty())
                continue;
            if (modIdx >= trackModState.modifiers.size())
                break;

            auto& modifier = trackModState.modifiers[modIdx];
            if (!modifier) {
                ++modIdx;
                continue;
            }

            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                auto& snapHolder = trackModState.curveSnapshots[modInfo.id];
                if (!snapHolder)
                    snapHolder = std::make_unique<CurveSnapshotHolder>();
                applyLFOProperties(lfo, modInfo, snapHolder.get());
            }

            // Update assignment values
            for (const auto& link : modInfo.links) {
                if (!link.isValid())
                    continue;

                te::Plugin::Ptr linkTarget;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = syncedDevices_.find(link.target.deviceId);
                    if (pit != syncedDevices_.end())
                        linkTarget = pit->second.plugin;
                }
                if (!linkTarget) {
                    auto* inner = instrumentRackManager_.getInnerPlugin(link.target.deviceId);
                    if (inner)
                        linkTarget = inner;
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
                                float effectiveAmount = link.amount;
                                if (!renderingActive_ &&
                                    modInfo.triggerMode != LFOTriggerMode::Free &&
                                    !modInfo.running) {
                                    if (!modInfo.oneShot || modInfo.phase < 1.0f)
                                        effectiveAmount = 0.0f;
                                }
                                assignment->value = effectiveAmount;
                                assignment->offset = 0.0f;
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

        // Check if any mod has active links AND device is not bypassed
        bool hasActiveMods = false;
        if (!device.bypassed) {
            for (const auto& mod : device.mods) {
                if (mod.enabled && !mod.links.empty()) {
                    hasActiveMods = true;
                    break;
                }
            }
        }

        // Choose the right ModifierList scope for parameter assignment.
        // Instruments are wrapped in an InstrumentRack — modifiers must live on
        // the rack's ModifierList to reach the inner plugin's parameters.
        // Standalone plugins live directly on the track, so use the track's list.
        te::ModifierList* modList = nullptr;
        if (device.isInstrument) {
            auto rackType = instrumentRackManager_.getRackType(device.id);
            if (rackType)
                modList = &rackType->getModifierList();
        }
        if (!modList)
            modList = teTrack->getModifierList();

        // Remove existing TE modifiers for this device before recreating
        auto& existingMods = syncedDevices_[device.id].modifiers;
        if (!existingMods.empty()) {
            // Clear LFO callbacks before destroying CurveSnapshotHolders
            clearLFOCustomWaveCallbacks(existingMods);
            for (auto& mod : existingMods) {
                if (!mod)
                    continue;

                // Remove modifier assignments from ALL plugins on the track
                // (not just the target plugin) to catch any cross-device assignments
                for (int pi = 0; pi < teTrack->pluginList.size(); ++pi) {
                    if (auto* plugin = teTrack->pluginList[pi]) {
                        for (auto* param : plugin->getAutomatableParameters())
                            param->removeModifier(*mod);
                    }
                }

                // Also remove from rack inner plugins (instruments)
                if (device.isInstrument) {
                    if (auto* inner = instrumentRackManager_.getInnerPlugin(device.id)) {
                        for (auto* param : inner->getAutomatableParameters())
                            param->removeModifier(*mod);
                    }
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
            auto sdIt = syncedDevices_.find(device.id);
            if (sdIt != syncedDevices_.end())
                targetPlugin = sdIt->second.plugin;
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
                        auto& snapHolder = syncedDevices_[device.id].curveSnapshots[modInfo.id];
                        if (!snapHolder)
                            snapHolder = std::make_unique<CurveSnapshotHolder>();
                        applyLFOProperties(lfo, modInfo, snapHolder.get());
                        // Cross-track sidechain LFOs must not resync from the
                        // destination track's own MIDI — they are triggered
                        // externally via triggerSidechainNoteOn().
                        if (device.sidechain.sourceTrackId != INVALID_TRACK_ID)
                            lfo->setSkipNativeResync(true);
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
                    auto sdIt2 = syncedDevices_.find(link.target.deviceId);
                    if (sdIt2 != syncedDevices_.end())
                        linkTarget = sdIt2->second.plugin;
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
                        if (!renderingActive_ && modInfo.triggerMode != LFOTriggerMode::Free &&
                            !modInfo.running) {
                            // Gate stopped LFOs, but let completed 1-shot hold
                            if (!modInfo.oneShot || modInfo.phase < 1.0f)
                                initialAmount = 0.0f;
                        }
                        param->addModifier(*modifier, initialAmount);
                    }
                }
            }
        }
    }

    // ---- Track-level mods (global scope: can target any device on the track) ----
    auto& trackModState = trackModStates_[trackId];

    // Remove existing TE modifiers for track-level mods
    if (!trackModState.modifiers.empty()) {
        clearLFOCustomWaveCallbacks(trackModState.modifiers);
        auto* trackModList = teTrack->getModifierList();
        for (auto& mod : trackModState.modifiers) {
            if (!mod)
                continue;
            // Remove assignments from all plugins on this track
            for (int pi = 0; pi < teTrack->pluginList.size(); ++pi) {
                if (auto* plugin = teTrack->pluginList[pi]) {
                    for (auto* param : plugin->getAutomatableParameters())
                        param->removeModifier(*mod);
                }
            }
            // Also remove from instrument rack inner plugins
            for (const auto& el : trackInfo->chainElements) {
                if (!isDevice(el))
                    continue;
                const auto& dev = getDevice(el);
                if (dev.isInstrument) {
                    if (auto* inner = instrumentRackManager_.getInnerPlugin(dev.id)) {
                        for (auto* param : inner->getAutomatableParameters())
                            param->removeModifier(*mod);
                    }
                }
            }
            if (trackModList)
                trackModList->state.removeChild(mod->state, nullptr);
        }
    }
    trackModState.modifiers.clear();

    // Check if any track-level mod has active links
    bool hasActiveTrackMods = false;
    for (const auto& modInfo : trackInfo->mods) {
        if (modInfo.enabled && !modInfo.links.empty()) {
            hasActiveTrackMods = true;
        }
    }

    if (hasActiveTrackMods) {
        auto* trackModList = teTrack->getModifierList();
        if (trackModList) {
            for (const auto& modInfo : trackInfo->mods) {
                if (!modInfo.enabled || modInfo.links.empty())
                    continue;

                te::Modifier::Ptr modifier;
                switch (modInfo.type) {
                    case ModType::LFO: {
                        juce::ValueTree lfoState(te::IDs::LFO);
                        auto lfoMod = trackModList->insertModifier(lfoState, -1, nullptr);
                        if (!lfoMod)
                            break;
                        if (auto* lfo = dynamic_cast<te::LFOModifier*>(lfoMod.get())) {
                            auto& snapHolder = trackModState.curveSnapshots[modInfo.id];
                            if (!snapHolder)
                                snapHolder = std::make_unique<CurveSnapshotHolder>();
                            applyLFOProperties(lfo, modInfo, snapHolder.get());
                        }
                        modifier = lfoMod;
                        break;
                    }
                    case ModType::Random: {
                        juce::ValueTree randomState(te::IDs::RANDOM);
                        modifier = trackModList->insertModifier(randomState, -1, nullptr);
                        break;
                    }
                    case ModType::Follower: {
                        juce::ValueTree envState(te::IDs::ENVELOPEFOLLOWER);
                        modifier = trackModList->insertModifier(envState, -1, nullptr);
                        break;
                    }
                    case ModType::Envelope:
                        break;
                }

                if (!modifier)
                    continue;

                trackModState.modifiers.push_back(modifier);

                // Create assignments — track mods can target any device on the track
                for (const auto& link : modInfo.links) {
                    if (!link.isValid())
                        continue;

                    te::Plugin::Ptr linkTarget;
                    {
                        juce::ScopedLock lock(pluginLock_);
                        auto sdIt = syncedDevices_.find(link.target.deviceId);
                        if (sdIt != syncedDevices_.end())
                            linkTarget = sdIt->second.plugin;
                    }
                    // For instruments, use the inner plugin
                    if (!linkTarget) {
                        auto* inner = instrumentRackManager_.getInnerPlugin(link.target.deviceId);
                        if (inner)
                            linkTarget = inner;
                    }
                    if (!linkTarget)
                        continue;

                    auto params = linkTarget->getAutomatableParameters();
                    if (link.target.paramIndex >= 0 &&
                        link.target.paramIndex < static_cast<int>(params.size())) {
                        auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                        if (param) {
                            float initialAmount = link.amount;
                            if (!renderingActive_ && modInfo.triggerMode != LFOTriggerMode::Free &&
                                !modInfo.running) {
                                if (!modInfo.oneShot || modInfo.phase < 1.0f)
                                    initialAmount = 0.0f;
                            }
                            param->addModifier(*modifier, initialAmount);
                        }
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

        // Skip sidechain LFOs — they are triggered separately via
        // triggerSidechainNoteOn from the source track's note events.
        // Triggering them here would reset the TE LFO phase mid-cycle,
        // causing false wrap-around detection in one-shot mode.
        if (device.sidechain.sourceTrackId != INVALID_TRACK_ID)
            continue;

        auto it = syncedDevices_.find(device.id);
        if (it == syncedDevices_.end())
            continue;

        for (auto& mod : it->second.modifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                lfo->triggerNoteOn();
            }
        }
    }

    // Also trigger LFOs inside MAGDA racks on this track
    rackSyncManager_.triggerLFONoteOn(trackId);

    // Also trigger track-level LFOs
    auto tmIt = trackModStates_.find(trackId);
    if (tmIt != trackModStates_.end()) {
        for (auto& mod : tmIt->second.modifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                lfo->triggerNoteOn();
            }
        }
    }
}

// =============================================================================
void PluginManager::triggerSidechainNoteOn(TrackId sourceTrackId) {
    if (sourceTrackId < 0 || sourceTrackId >= kMaxCacheTracks)
        return;

    const juce::SpinLock::ScopedTryLockType lock(cacheLock_);
    if (!lock.isLocked())
        return;  // Cache is being rebuilt — skip this trigger

    auto& entry = sidechainLFOCache_[static_cast<size_t>(sourceTrackId)];
    for (int i = 0; i < entry.count; ++i) {
        auto* lfo = entry.lfos[static_cast<size_t>(i)];
        bool crossTrack = entry.isCrossTrack[static_cast<size_t>(i)];
        // Cross-track: force value=0 for transient gap.
        // Self-track: resync phase but preserve value (no zero gap needed).
        triggerLFONoteOnWithReset(lfo, crossTrack);
    }
}

void PluginManager::gateSidechainLFOs(TrackId sourceTrackId) {
    if (sourceTrackId < 0 || sourceTrackId >= kMaxCacheTracks)
        return;

    const juce::SpinLock::ScopedTryLockType lock(cacheLock_);
    if (!lock.isLocked())
        return;

    auto& entry = sidechainLFOCache_[static_cast<size_t>(sourceTrackId)];
    for (int i = 0; i < entry.count; ++i) {
        // Only gate cross-track (sidechain destination) LFOs.
        // Self-track LFOs should free-run and just reset phase on noteOn.
        if (!entry.isCrossTrack[static_cast<size_t>(i)])
            continue;
        auto* lfo = entry.lfos[static_cast<size_t>(i)];
        // Only gate note-triggered LFOs (syncType == 2)
        if (juce::roundToInt(lfo->syncTypeParam->getCurrentValue()) == 2)
            lfo->setGated(true);
    }
}

void PluginManager::prepareForRendering() {
    renderingActive_ = true;
    rackSyncManager_.setRenderingActive(true);

    // Reset sidechain monitors so held-note counts from playback don't
    // carry over into the render pass (which would prevent gating).
    for (auto& [trackId, pluginPtr] : sidechainMonitors_) {
        if (auto* monitor = dynamic_cast<SidechainMonitorPlugin*>(pluginPtr.get()))
            monitor->reset();
    }

    // Re-enable tone generators that were bypassed when transport stopped.
    // Must happen before the render graph is built so they appear as enabled.
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(sd.processor.get())) {
                toneProc->setBypassed(false);
            }
        }
    }

    // Enable all MIDI/Audio-triggered LFO assignments so modulation is active
    // during offline rendering. The message-thread timer gating can't keep up
    // with the renderer's speed, so we disable gating entirely.
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        updateDeviceModifierProperties(track.id);
        rackSyncManager_.updateAllModifierProperties(track.id);
    }
}

void PluginManager::restoreAfterRendering() {
    renderingActive_ = false;
    rackSyncManager_.setRenderingActive(false);
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
        int selfTrackCount = 0;  // track how many are self-track (added first)

        // 1. Self-track LFOs: collect from syncedDevices_ modifiers for this track's devices
        //    Skip devices that have a cross-track sidechain source — those LFOs
        //    are triggered by the source track, not by self.
        for (const auto& element : track.chainElements) {
            if (!isDevice(element))
                continue;
            const auto& device = getDevice(element);
            if (device.sidechain.sourceTrackId != INVALID_TRACK_ID)
                continue;  // Has external sidechain — skip self-triggering
            auto it = syncedDevices_.find(device.id);
            if (it == syncedDevices_.end())
                continue;
            for (auto& mod : it->second.modifiers) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get()))
                    lfos.push_back(lfo);
            }
        }

        // Also collect from racks on this track (skip racks with external sidechain)
        {
            bool hasRackSidechain = false;
            for (const auto& element : track.chainElements) {
                if (isRack(element)) {
                    const auto& rack = getRack(element);
                    if (rack.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                        hasRackSidechain = true;
                        break;
                    }
                    // Also check devices inside rack for sidechain sources
                    for (const auto& chain : rack.chains) {
                        for (const auto& ce : chain.elements) {
                            if (isDevice(ce) &&
                                getDevice(ce).sidechain.sourceTrackId != INVALID_TRACK_ID) {
                                hasRackSidechain = true;
                                break;
                            }
                        }
                        if (hasRackSidechain)
                            break;
                    }
                }
            }
            if (!hasRackSidechain)
                rackSyncManager_.collectLFOModifiers(track.id, lfos);
        }

        selfTrackCount = static_cast<int>(lfos.size());

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
                } else if (isRack(element)) {
                    const auto& rack = getRack(element);
                    // Check rack-level sidechain
                    if ((rack.sidechain.type == SidechainConfig::Type::MIDI ||
                         rack.sidechain.type == SidechainConfig::Type::Audio) &&
                        rack.sidechain.sourceTrackId == track.id) {
                        isDestination = true;
                        break;
                    }
                    for (const auto& chain : rack.chains) {
                        for (const auto& ce : chain.elements) {
                            if (isDevice(ce)) {
                                const auto& device = getDevice(ce);
                                if ((device.sidechain.type == SidechainConfig::Type::MIDI ||
                                     device.sidechain.type == SidechainConfig::Type::Audio) &&
                                    device.sidechain.sourceTrackId == track.id) {
                                    isDestination = true;
                                    break;
                                }
                            }
                        }
                        if (isDestination)
                            break;
                    }
                }
                if (isDestination)
                    break;
            }
            if (!isDestination)
                continue;

            // Collect LFO modifiers only from devices on the destination track
            // that are actually sidechained from this source track.
            for (const auto& element : otherTrack.chainElements) {
                if (!isDevice(element))
                    continue;
                const auto& device = getDevice(element);
                // Only collect from devices whose sidechain source is this track
                if (device.sidechain.sourceTrackId != track.id)
                    continue;
                auto it = syncedDevices_.find(device.id);
                if (it == syncedDevices_.end())
                    continue;
                for (auto& mod : it->second.modifiers) {
                    if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get()))
                        lfos.push_back(lfo);
                }
            }
            // TODO: also filter rack LFOs by sidechain source
            rackSyncManager_.collectLFOModifiers(otherTrack.id, lfos);
        }

        // Write to cache entry (capped at kMaxLFOs)
        // Self-track LFOs come first (indices 0..selfTrackCount-1),
        // cross-track LFOs follow (indices selfTrackCount..count-1).
        entry.count = std::min(static_cast<int>(lfos.size()), PerTrackEntry::kMaxLFOs);
        for (int i = 0; i < entry.count; ++i) {
            entry.lfos[static_cast<size_t>(i)] = lfos[static_cast<size_t>(i)];
            entry.isCrossTrack[static_cast<size_t>(i)] = (i >= selfTrackCount);
        }
    }

    // Swap under lock
    {
        const juce::SpinLock::ScopedLockType lock(cacheLock_);
        sidechainLFOCache_ = newCache;
    }
}

// =============================================================================
std::pair<int, int> PluginManager::computeModLinkFingerprint(TrackId trackId,
                                                             const TrackInfo* trackInfo) const {
    if (!trackInfo)
        return {0, 0};

    int modCount = 0, linkCount = 0, bipolarCount = 0;

    // Device-level mods
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;
        const auto& device = getDevice(element);
        for (const auto& mod : device.mods) {
            if (mod.enabled && !mod.links.empty()) {
                ++modCount;
                linkCount += static_cast<int>(mod.links.size());
                for (const auto& link : mod.links)
                    bipolarCount += link.bipolar ? 1 : 0;
            }
        }
        // Device-level macros
        for (const auto& macro : device.macros) {
            if (!macro.links.empty()) {
                ++modCount;
                linkCount += static_cast<int>(macro.links.size());
                for (const auto& link : macro.links)
                    bipolarCount += link.bipolar ? 1 : 0;
            }
        }
    }

    // Track-level mods
    for (const auto& mod : trackInfo->mods) {
        if (mod.enabled && !mod.links.empty()) {
            ++modCount;
            linkCount += static_cast<int>(mod.links.size());
            for (const auto& link : mod.links)
                bipolarCount += link.bipolar ? 1 : 0;
        }
    }

    // Track-level macros
    for (const auto& macro : trackInfo->macros) {
        if (!macro.links.empty()) {
            ++modCount;
            linkCount += static_cast<int>(macro.links.size());
            for (const auto& link : macro.links)
                bipolarCount += link.bipolar ? 1 : 0;
        }
    }

    return {modCount, linkCount + (bipolarCount << 16)};
}

// =============================================================================
void PluginManager::resyncDeviceModifiers(TrackId trackId) {
    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (teTrack) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        auto currentFP = computeModLinkFingerprint(trackId, trackInfo);
        auto& storedFP = modLinkFingerprints_[trackId];

        if (currentFP != storedFP) {
            // Link structure changed — full rebuild
            storedFP = currentFP;
            syncDeviceModifiers(trackId, teTrack);
            syncDeviceMacros(trackId, teTrack);
        } else {
            // Properties only changed (rate, waveform, etc.) — update in-place
            updateDeviceModifierProperties(trackId);
        }
    }
    rackSyncManager_.resyncAllModifiers(trackId);
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
        // Check if this is a track-level macro (id == trackId, stored in trackMacroParams_)
        auto tmIt = trackMacroParams_.find(static_cast<TrackId>(id));
        if (tmIt != trackMacroParams_.end()) {
            auto macroIt = tmIt->second.find(macroIndex);
            if (macroIt != tmIt->second.end() && macroIt->second != nullptr) {
                macroIt->second->setParameter(value, juce::sendNotificationSync);
                return;
            }
        }
        // Device macro — use device macro params
        setDeviceMacroValue(static_cast<DeviceId>(id), macroIndex, value);
    }
}

void PluginManager::setDeviceMacroValue(DeviceId deviceId, int macroIndex, float value) {
    auto it = syncedDevices_.find(deviceId);
    if (it == syncedDevices_.end())
        return;

    auto macroIt = it->second.macroParams.find(macroIndex);
    if (macroIt != it->second.macroParams.end() && macroIt->second != nullptr) {
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

        auto sdIt = syncedDevices_.find(device.id);
        if (sdIt != syncedDevices_.end() && !sdIt->second.macroParams.empty()) {
            for (auto& [macroIdx, macroParam] : sdIt->second.macroParams) {
                if (!macroParam)
                    continue;

                // Remove modifier assignments from all plugin params on this track
                // (includes both track-level devices and DrumGrid pad chain plugins)
                auto removeModFromSynced = [&](DeviceId devId) {
                    te::Plugin::Ptr plugin;
                    {
                        juce::ScopedLock lock(pluginLock_);
                        auto pit = syncedDevices_.find(devId);
                        if (pit != syncedDevices_.end())
                            plugin = pit->second.plugin;
                    }
                    if (plugin) {
                        for (auto* param : plugin->getAutomatableParameters())
                            param->removeModifier(*macroParam);
                    }
                };

                for (const auto& el : trackInfo->chainElements) {
                    if (!isDevice(el))
                        continue;
                    const auto& dev = getDevice(el);
                    removeModFromSynced(dev.id);

                    // Also remove from pad chain plugins if this is a DrumGrid
                    auto dgIt = drumGridPadDevices_.find(dev.id);
                    if (dgIt != drumGridPadDevices_.end()) {
                        for (auto padDevId : dgIt->second)
                            removeModFromSynced(padDevId);
                    }
                }

                macroList.removeMacroParameter(*macroParam);
            }
            sdIt->second.macroParams.clear();
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

            syncedDevices_[device.id].macroParams[i] = macroParam;

            // Create assignments for each link
            for (const auto& link : macroInfo.links) {
                if (!link.target.isValid())
                    continue;

                // Find the TE plugin for the link target device
                te::Plugin::Ptr linkTarget;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = syncedDevices_.find(link.target.deviceId);
                    if (pit != syncedDevices_.end())
                        linkTarget = pit->second.plugin;
                }
                if (!linkTarget) {
                    auto* inner = instrumentRackManager_.getInnerPlugin(link.target.deviceId);
                    if (inner)
                        linkTarget = inner;
                }
                if (!linkTarget)
                    continue;

                auto params = linkTarget->getAutomatableParameters();
                if (link.target.paramIndex >= 0 &&
                    link.target.paramIndex < static_cast<int>(params.size())) {
                    auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                    if (param) {
                        // Bipolar: macro 0→-amount, 0.5→0, 1→+amount
                        // Unipolar: macro 0→0, 1→+amount
                        float offset = link.bipolar ? -link.amount : 0.0f;
                        float value = link.bipolar ? link.amount * 2.0f : link.amount;
                        param->addModifier(*macroParam, value, offset);
                    }
                }
            }
        }
    }

    // ---- Track-level macros (can target any device on the track) ----

    // Remove existing track-level macro params
    auto& existingTrackMacros = trackMacroParams_[trackId];
    if (!existingTrackMacros.empty()) {
        for (auto& [macroIdx, macroParam] : existingTrackMacros) {
            if (!macroParam)
                continue;

            // Remove assignments from all plugins on this track
            // (includes pad chain plugins inside DrumGrid devices)
            auto removeModFromSynced2 = [&](DeviceId devId) {
                te::Plugin::Ptr plugin;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pit = syncedDevices_.find(devId);
                    if (pit != syncedDevices_.end())
                        plugin = pit->second.plugin;
                }
                if (plugin) {
                    for (auto* param : plugin->getAutomatableParameters())
                        param->removeModifier(*macroParam);
                }
            };

            for (const auto& el : trackInfo->chainElements) {
                if (!isDevice(el))
                    continue;
                const auto& dev = getDevice(el);
                removeModFromSynced2(dev.id);

                // Also remove from pad chain plugins if this is a DrumGrid
                auto dgIt = drumGridPadDevices_.find(dev.id);
                if (dgIt != drumGridPadDevices_.end()) {
                    for (auto padDevId : dgIt->second)
                        removeModFromSynced2(padDevId);
                }

                // Also check instrument inner plugins
                if (dev.isInstrument) {
                    if (auto* inner = instrumentRackManager_.getInnerPlugin(dev.id)) {
                        for (auto* param : inner->getAutomatableParameters())
                            param->removeModifier(*macroParam);
                    }
                }
            }

            macroList.removeMacroParameter(*macroParam);
        }
        existingTrackMacros.clear();
    }

    // Create TE MacroParameters for each linked track-level macro
    for (int i = 0; i < static_cast<int>(trackInfo->macros.size()); ++i) {
        const auto& macroInfo = trackInfo->macros[static_cast<size_t>(i)];
        if (!macroInfo.isLinked())
            continue;

        auto* macroParam = macroList.createMacroParameter();
        if (!macroParam)
            continue;

        macroParam->macroName = macroInfo.name;
        macroParam->setParameter(macroInfo.value, juce::dontSendNotification);

        existingTrackMacros[i] = macroParam;

        for (const auto& link : macroInfo.links) {
            if (!link.target.isValid())
                continue;

            te::Plugin::Ptr linkTarget;
            {
                juce::ScopedLock lock(pluginLock_);
                auto pit = syncedDevices_.find(link.target.deviceId);
                if (pit != syncedDevices_.end())
                    linkTarget = pit->second.plugin;
            }
            if (!linkTarget) {
                auto* inner = instrumentRackManager_.getInnerPlugin(link.target.deviceId);
                if (inner)
                    linkTarget = inner;
            }
            if (!linkTarget)
                continue;

            auto params = linkTarget->getAutomatableParameters();
            if (link.target.paramIndex >= 0 &&
                link.target.paramIndex < static_cast<int>(params.size())) {
                auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                if (param) {
                    float offset = link.bipolar ? -link.amount : 0.0f;
                    float value = link.bipolar ? link.amount * 2.0f : link.amount;
                    param->addModifier(*macroParam, value, offset);
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

        // --- Audio sidechain (TE native) ---
        if (plugin && plugin->canSidechain()) {
            if (device.sidechain.isActive() &&
                device.sidechain.type == SidechainConfig::Type::Audio) {
                auto* sourceTrack = trackController_.getAudioTrack(device.sidechain.sourceTrackId);
                if (sourceTrack) {
                    plugin->setSidechainSourceID(sourceTrack->itemID);
                    plugin->guessSidechainRouting();
                }
            } else {
                plugin->setSidechainSourceID({});
            }
        }

        // --- MIDI sidechain (MidiReceivePlugin injection) ---
        if (device.sidechain.isActive() && device.sidechain.type == SidechainConfig::Type::MIDI) {
            ensureMidiReceive(trackId, device.id, device.sidechain.sourceTrackId);
        } else {
            removeMidiReceive(trackId, device.id);
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
            const auto& rack = getRack(element);
            for (const auto& mod : rack.mods) {
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
            } else if (isRack(element)) {
                const auto& rack = getRack(element);
                // Check rack-level sidechain
                if (rack.sidechain.type == SidechainConfig::Type::MIDI &&
                    rack.sidechain.sourceTrackId == trackId) {
                    return true;
                }
                for (const auto& chain : rack.chains) {
                    for (const auto& ce : chain.elements) {
                        if (isDevice(ce)) {
                            const auto& device = getDevice(ce);
                            if (device.sidechain.type == SidechainConfig::Type::MIDI &&
                                device.sidechain.sourceTrackId == trackId) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

void PluginManager::checkSidechainMonitor(TrackId trackId) {
    bool needed = trackNeedsSidechainMonitor(trackId);
    if (needed)
        ensureSidechainMonitor(trackId);
    else
        removeSidechainMonitor(trackId);

    rebuildSidechainLFOCache();
}

void PluginManager::ensureSidechainMonitor(TrackId sourceTrackId) {
    // Already have a monitor for this track?
    if (sidechainMonitors_.count(sourceTrackId) > 0)
        return;

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

// =============================================================================
// MIDI Receive Plugin Lifecycle
// =============================================================================

void PluginManager::ensureMidiReceive(TrackId trackId, DeviceId deviceId, TrackId sourceTrackId) {
    // Already have one for this device? Update its source track if needed.
    auto it = syncedDevices_.find(deviceId);
    if (it != syncedDevices_.end() && it->second.midiReceivePlugin) {
        if (auto* rx = dynamic_cast<MidiReceivePlugin*>(it->second.midiReceivePlugin.get())) {
            if (rx->getSourceTrackId() != sourceTrackId) {
                rx->setSourceTrackId(sourceTrackId);
                auto* sourceTeTrack = trackController_.getAudioTrack(sourceTrackId);
                if (sourceTeTrack) {
                    it->second.midiReceivePlugin->setSidechainSourceID(sourceTeTrack->itemID);
                    it->second.midiReceivePlugin->guessSidechainRouting();
                }
            }
        }
        return;
    }

    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (!teTrack)
        return;

    // Find the target device's TE plugin to insert before it
    auto targetPlugin = getPlugin(deviceId);
    int insertPos = -1;
    if (targetPlugin) {
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            if (teTrack->pluginList[i] == targetPlugin.get()) {
                insertPos = i;
                break;
            }
        }
    }

    // Create MidiReceivePlugin via TE plugin cache
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, MidiReceivePlugin::xmlTypeName, nullptr);
    pluginState.setProperty(juce::Identifier("sourceTrackId"), sourceTrackId, nullptr);

    auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    if (plugin) {
        if (auto* rx = dynamic_cast<MidiReceivePlugin*>(plugin.get()))
            rx->setSourceTrackId(sourceTrackId);

        // Set sidechain source to create a graph dependency on the source track.
        // This ensures TE processes the source track (with SidechainMonitorPlugin)
        // before this plugin, so MidiBroadcastBus contains current-block MIDI — zero latency.
        auto* sourceTeTrack = trackController_.getAudioTrack(sourceTrackId);
        if (sourceTeTrack) {
            plugin->setSidechainSourceID(sourceTeTrack->itemID);
            plugin->guessSidechainRouting();
        }

        teTrack->pluginList.insertPlugin(plugin, insertPos, nullptr);
        syncedDevices_[deviceId].trackId = trackId;
        syncedDevices_[deviceId].midiReceivePlugin = plugin;
        DBG("PluginManager::ensureMidiReceive - inserted MidiReceivePlugin for device "
            << deviceId << " source=" << sourceTrackId << " at pos=" << insertPos);
    }
}

void PluginManager::removeMidiReceive(TrackId /*trackId*/, DeviceId deviceId) {
    auto it = syncedDevices_.find(deviceId);
    if (it == syncedDevices_.end() || !it->second.midiReceivePlugin)
        return;

    DBG("PluginManager::removeMidiReceive - removing for device " << deviceId);
    auto plugin = it->second.midiReceivePlugin;
    it->second.midiReceivePlugin = nullptr;

    if (plugin)
        plugin->deleteFromParent();
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
// Plugin State Capture/Restore
// =============================================================================

void PluginManager::captureAllPluginStates() {
    juce::ScopedLock lock(pluginLock_);

    for (const auto& [deviceId, sd] : syncedDevices_) {
        if (!sd.plugin)
            continue;

        juce::String stateStr;

        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(sd.plugin.get())) {
            // External plugin: capture base64 blob from TE state property
            ext->flushPluginStateToValueTree();
            stateStr = ext->state.getProperty(te::IDs::state).toString();
        } else {
            // TE internal plugin (4osc, EQ, Compressor, etc.):
            // Capture the full ValueTree as XML so non-automatable
            // CachedValues (wave shapes, filter type, etc.) are preserved.
            sd.plugin->flushPluginStateToValueTree();
            if (auto xml = sd.plugin->state.createXml())
                stateStr = xml->toString();
        }

        // Always overwrite pluginState (even if empty) to avoid stale state
        auto& trackManager = TrackManager::getInstance();
        for (auto& track : trackManager.getTracks()) {
            if (auto* devInfo = trackManager.getDevice(track.id, deviceId)) {
                devInfo->pluginState = stateStr;
                break;
            }
        }
    }

    // Also capture state from plugins inside racks
    rackSyncManager_.captureAllPluginStates();
}

void PluginManager::capturePluginState(DeviceId deviceId) {
    juce::ScopedLock lock(pluginLock_);

    auto it = syncedDevices_.find(deviceId);
    if (it == syncedDevices_.end() || !it->second.plugin) {
        DBG("capturePluginState: device " << deviceId << " not found in syncedDevices");
        return;
    }

    auto* plugin = it->second.plugin.get();
    juce::String stateStr;

    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin)) {
        ext->flushPluginStateToValueTree();
        stateStr = ext->state.getProperty(te::IDs::state).toString();
        DBG("capturePluginState: external plugin, state length=" << stateStr.length());
    } else {
        plugin->flushPluginStateToValueTree();
        if (auto xml = plugin->state.createXml())
            stateStr = xml->toString();
        DBG("capturePluginState: internal plugin, state length=" << stateStr.length());
    }

    bool found = false;
    auto& trackManager = TrackManager::getInstance();
    for (auto& track : trackManager.getTracks()) {
        if (auto* devInfo = trackManager.getDevice(track.id, deviceId)) {
            devInfo->pluginState = stateStr;
            found = true;
            DBG("capturePluginState: saved to DeviceInfo on track " << track.id);
            break;
        }
    }
    if (!found) {
        DBG("capturePluginState: WARNING - device " << deviceId << " not found in any track");
    }
}

void PluginManager::restorePluginState(TrackId trackId, DeviceId deviceId, te::Plugin::Ptr plugin) {
    auto* devInfo = TrackManager::getInstance().getDevice(trackId, deviceId);
    if (!devInfo || devInfo->pluginState.isEmpty()) {
        DBG("restorePluginState: no state to restore for device "
            << deviceId << " (devInfo=" << (devInfo ? "found" : "null") << ", state="
            << (devInfo ? juce::String(devInfo->pluginState.length()) : "n/a") << ")");
        return;
    }

    DBG("restorePluginState: restoring device "
        << deviceId << " on track " << trackId
        << ", state length=" << devInfo->pluginState.length());

    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        ext->state.setProperty(te::IDs::state, devInfo->pluginState, nullptr);
        DBG("restorePluginState: set external plugin state property");
    } else {
        // Internal plugin: restore from saved XML ValueTree
        if (auto xml = juce::parseXML(devInfo->pluginState)) {
            auto savedState = juce::ValueTree::fromXml(*xml);
            if (savedState.isValid()) {
                plugin->restorePluginStateFromValueTree(savedState);
                DBG("restorePluginState: restored internal plugin from XML");
            }
        }
    }
}

void PluginManager::purgeStaleEntries() {
    auto& tm = TrackManager::getInstance();

    // Collect all valid DeviceIds from TrackManager (including rack inner devices)
    std::set<DeviceId> validDeviceIds;
    std::set<TrackId> validTrackIds;
    std::set<RackId> validRackIds;

    for (const auto& track : tm.getTracks()) {
        validTrackIds.insert(track.id);

        std::function<void(const std::vector<ChainElement>&)> collectIds;
        collectIds = [&](const std::vector<ChainElement>& elements) {
            for (const auto& element : elements) {
                if (isDevice(element)) {
                    validDeviceIds.insert(getDevice(element).id);
                } else if (isRack(element)) {
                    const auto& rack = getRack(element);
                    validRackIds.insert(rack.id);
                    for (const auto& chain : rack.chains) {
                        collectIds(chain.elements);
                    }
                }
            }
        };
        collectIds(track.chainElements);
    }

    // Purge stale entries from maps
    int purged = 0;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);

        // syncedDevices_ (consolidates all per-device maps)
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (auto it = syncedDevices_.begin(); it != syncedDevices_.end();) {
            if (validDeviceIds.find(it->first) == validDeviceIds.end()) {
                // Clear LFO callbacks before destroying CurveSnapshotHolders
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get()))
                    dg->removeListener(this);
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                if (it->second.midiReceivePlugin)
                    pluginsToDelete.push_back(it->second.midiReceivePlugin);
                it = syncedDevices_.erase(it);
                ++purged;
            } else {
                ++it;
            }
        }

        // sidechainMonitors_ (keyed by TrackId) — collect for deletion outside lock
        for (auto it = sidechainMonitors_.begin(); it != sidechainMonitors_.end();) {
            if (validTrackIds.find(it->first) == validTrackIds.end()) {
                if (it->second)
                    pluginsToDelete.push_back(it->second);
                it = sidechainMonitors_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Delete plugins outside the lock to avoid blocking and re-entrancy
    for (auto& plugin : pluginsToDelete) {
        plugin->deleteFromParent();
    }

    // Remove stale synced racks
    auto syncedRackIds = rackSyncManager_.getSyncedRackIds();
    for (auto rackId : syncedRackIds) {
        if (validRackIds.find(rackId) == validRackIds.end()) {
            rackSyncManager_.removeRack(rackId);
            ++purged;
        }
    }

    if (purged > 0) {
        DBG("PluginManager::purgeStaleEntries - purged " << purged << " stale entries");
        rebuildSidechainLFOCache();
    }
}

void PluginManager::validateMappingConsistency() {
#if JUCE_DEBUG
    juce::ScopedLock lock(pluginLock_);

    // 1. Every syncedDevices_ entry's plugin owner track should exist in TrackController
    for (const auto& [deviceId, sd] : syncedDevices_) {
        if (!sd.plugin) {
            continue;  // Some entries may only have processor (rack inner plugins)
        }
        auto* owner = sd.plugin->getOwnerTrack();
        if (owner) {
            bool found = false;
            for (auto trackId : trackController_.getAllTrackIds()) {
                if (trackController_.getAudioTrack(trackId) == owner) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                DBG("validateMappingConsistency WARNING: deviceId="
                    << deviceId << " has plugin on unknown TE track");
            }
        }
    }

    // 2. (removed — struct guarantees modifier ↔ plugin consistency)

    // 3. Every sidechainMonitors_ TrackId should exist in TrackController
    for (const auto& [trackId, plugin] : sidechainMonitors_) {
        if (trackController_.getAudioTrack(trackId) == nullptr) {
            DBG("validateMappingConsistency WARNING: sidechainMonitors_ has orphan trackId="
                << trackId);
        }
    }

    // 4. Every synced rack's trackId should exist in TrackController
    auto syncedRackIds = rackSyncManager_.getSyncedRackIds();
    for (auto rackId : syncedRackIds) {
        // Can't easily check trackId without exposing internals, but we can check
        // the rack exists in TrackManager
        bool found = false;
        for (const auto& track : TrackManager::getInstance().getTracks()) {
            for (const auto& element : track.chainElements) {
                if (isRack(element) && getRack(element).id == rackId) {
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found) {
            DBG("validateMappingConsistency WARNING: synced rackId="
                << rackId << " not found in TrackManager");
        }
    }
#endif
}

void PluginManager::clearAllMappings() {
    juce::ScopedLock lock(pluginLock_);
    // Clear LFO callbacks and defer holder destruction
    for (auto& [deviceId, sd] : syncedDevices_) {
        clearLFOCustomWaveCallbacks(sd.modifiers);
        deferCurveSnapshots(sd.curveSnapshots, deferredHolders_);
        // Unregister DrumGrid listener to avoid dangling pointer
        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(sd.plugin.get()))
            dg->removeListener(this);
        else if (auto* inner = instrumentRackManager_.getInnerPlugin(deviceId))
            if (auto* dg2 = dynamic_cast<daw::audio::DrumGridPlugin*>(inner))
                dg2->removeListener(this);
    }
    instrumentRackManager_.clear();
    rackSyncManager_.clear();
    syncedDevices_.clear();
    pluginToDevice_.clear();
    drumGridPadDevices_.clear();
    sidechainMonitors_.clear();
    // Drain deferred holders after all state is cleared (shutdown path —
    // audio engine is stopped so no in-flight callbacks remain)
    deferredHolders_.clear();
}

void PluginManager::updateTransportSyncedProcessors(bool isPlaying) {
    juce::ScopedLock lock(pluginLock_);

    // During offline rendering, keep tone generators enabled regardless of
    // transport state — the renderer drives playback independently.
    if (renderingActive_)
        return;

    for (const auto& [deviceId, sd] : syncedDevices_) {
        if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(sd.processor.get())) {
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
