#include "RackSyncManager.hpp"

#include "CurveSnapshot.hpp"
#include "ModifierHelpers.hpp"
#include "PluginManager.hpp"
#include "core/TrackManager.hpp"

namespace magda {

RackSyncManager::RackSyncManager(te::Edit& edit, PluginManager& pluginManager)
    : edit_(edit), pluginManager_(pluginManager) {}

// =============================================================================
// Public API
// =============================================================================

te::Plugin::Ptr RackSyncManager::syncRack(TrackId trackId, const RackInfo& rackInfo) {
    deferredHolders_.clear();  // Drain previous cycle's deferred holders

    DBG("syncRack: rackId=" << rackInfo.id << " trackId=" << trackId
                            << " chains=" << (int)rackInfo.chains.size() << " alreadySynced="
                            << (int)(syncedRacks_.find(rackInfo.id) != syncedRacks_.end()));

    // Check if already synced
    auto it = syncedRacks_.find(rackInfo.id);
    if (it != syncedRacks_.end()) {
        bool changed = structureChanged(it->second, rackInfo);
        DBG("syncRack: existing rack, structureChanged=" << (int)changed);
        if (changed) {
            resyncRack(trackId, rackInfo);
        } else {
            updateProperties(it->second, rackInfo);
        }
        return it->second.rackInstance;
    }

    // 1. Create a new RackType in the edit
    auto rackType = edit_.getRackList().addNewRack();
    if (!rackType) {
        DBG("RackSyncManager: Failed to create RackType for rack " << rackInfo.id);
        return nullptr;
    }

    rackType->rackName = rackInfo.name.isNotEmpty() ? rackInfo.name : "FX Rack";

    // 2. Set up SyncedRack state
    SyncedRack synced;
    synced.rackId = rackInfo.id;
    synced.trackId = trackId;
    synced.rackType = rackType;

    // 3. Load chain plugins into the RackType
    loadChainPlugins(synced, trackId, rackInfo);

    // 4. Build audio connections
    buildConnections(synced, rackInfo);

    // 5. Sync modifiers and macros (Phase 2)
    syncModifiers(synced, rackInfo);
    syncMacros(synced, rackInfo);

    // 6. Create a RackInstance from the RackType
    auto rackInstanceState = te::RackInstance::create(*rackType);
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        DBG("RackSyncManager: Failed to create RackInstance for rack " << rackInfo.id);
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    synced.rackInstance = rackInstance;

    // 7. Apply bypass state
    applyBypassState(synced, rackInfo);

    // 8. Store synced state
    syncedRacks_[rackInfo.id] = std::move(synced);

    DBG("RackSyncManager: Synced rack " << rackInfo.id << " ('" << rackInfo.name << "') with "
                                        << rackInfo.chains.size() << " chains");

    return rackInstance;
}

void RackSyncManager::resyncRack(TrackId trackId, const RackInfo& rackInfo) {
    auto it = syncedRacks_.find(rackInfo.id);
    if (it == syncedRacks_.end()) {
        // Not yet synced — do a full sync
        syncRack(trackId, rackInfo);
        return;
    }

    auto& synced = it->second;
    auto& rackType = synced.rackType;
    if (!rackType)
        return;

    // Capture current plugin states before teardown so they survive recreation
    capturePluginStates(synced);

    // Remove all existing connections (collect first to avoid iterator invalidation)
    {
        auto connections = rackType->getConnections();
        for (int i = connections.size(); --i >= 0;) {
            auto* conn = connections[i];
            rackType->removeConnection(conn->sourceID, conn->sourcePin, conn->destID,
                                       conn->destPin);
        }
    }

    // Remove old inner plugins from the rack
    for (auto& [deviceId, plugin] : synced.innerPlugins) {
        if (plugin)
            plugin->deleteFromParent();
    }
    synced.innerPlugins.clear();

    for (auto& [chainId, plugin] : synced.chainVolPanPlugins) {
        if (plugin)
            plugin->deleteFromParent();
    }
    synced.chainVolPanPlugins.clear();

    // Reload chain plugins and rebuild connections
    loadChainPlugins(synced, trackId, rackInfo);
    buildConnections(synced, rackInfo);

    // Resync modifiers and macros
    syncModifiers(synced, rackInfo);
    syncMacros(synced, rackInfo);

    // Reapply bypass
    applyBypassState(synced, rackInfo);

    DBG("RackSyncManager: Resynced rack " << rackInfo.id);
}

void RackSyncManager::updateRackProperties(const RackInfo& rackInfo) {
    auto it = syncedRacks_.find(rackInfo.id);
    if (it == syncedRacks_.end())
        return;
    updateProperties(it->second, rackInfo);
}

void RackSyncManager::removeRack(RackId rackId) {
    auto it = syncedRacks_.find(rackId);
    if (it == syncedRacks_.end())
        return;

    auto& synced = it->second;

    // Clear saved plugin state so re-adding the same device gets fresh defaults
    auto& trackManager = TrackManager::getInstance();
    for (auto& [deviceId, plugin] : synced.innerPlugins) {
        if (auto* devInfo = trackManager.getDevice(synced.trackId, deviceId)) {
            DBG("removeRack: clearing pluginState for deviceId="
                << deviceId
                << " stateWas=" << (devInfo->pluginState.isNotEmpty() ? "non-empty" : "empty"));
            devInfo->pluginState.clear();
        } else {
            DBG("removeRack: no devInfo for deviceId=" << deviceId << " (already removed?)");
        }
    }

    // Remove the RackInstance from its parent track
    if (synced.rackInstance) {
        synced.rackInstance->deleteFromParent();
    }

    // Remove the RackType from the edit
    if (synced.rackType) {
        edit_.getRackList().removeRackType(synced.rackType);
    }

    // Clear LFO callbacks and defer holder destruction
    clearLFOCustomWaveCallbacks(synced.innerModifiers);
    deferCurveSnapshots(synced.curveSnapshots, deferredHolders_);

    DBG("RackSyncManager: Removed rack " << rackId);

    syncedRacks_.erase(it);
}

void RackSyncManager::removeRacksForTrack(TrackId trackId) {
    std::vector<RackId> toRemove;
    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId == trackId)
            toRemove.push_back(rackId);
    }
    for (auto rackId : toRemove) {
        removeRack(rackId);
    }
}

std::vector<RackId> RackSyncManager::getSyncedRackIds() const {
    std::vector<RackId> ids;
    ids.reserve(syncedRacks_.size());
    for (const auto& [rackId, _] : syncedRacks_) {
        ids.push_back(rackId);
    }
    return ids;
}

std::vector<RackId> RackSyncManager::getSyncedRackIdsForTrack(TrackId trackId) const {
    std::vector<RackId> ids;
    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId == trackId)
            ids.push_back(rackId);
    }
    return ids;
}

std::vector<DeviceId> RackSyncManager::getInnerDeviceIdsForTrack(TrackId trackId) const {
    std::vector<DeviceId> ids;
    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;
        for (const auto& [deviceId, plugin] : synced.innerPlugins) {
            ids.push_back(deviceId);
        }
    }
    return ids;
}

std::unordered_map<TrackId, RackSyncManager::TrackMeteringInfo> RackSyncManager::getMeteringMap()
    const {
    std::unordered_map<TrackId, TrackMeteringInfo> map;
    for (const auto& [rackId, synced] : syncedRacks_) {
        auto& info = map[synced.trackId];
        info.rackIds.push_back(rackId);
        for (const auto& [deviceId, plugin] : synced.innerPlugins) {
            info.deviceIds.push_back(deviceId);
        }
    }
    return map;
}

te::Plugin* RackSyncManager::getInnerPlugin(DeviceId deviceId) const {
    for (const auto& [rackId, synced] : syncedRacks_) {
        auto it = synced.innerPlugins.find(deviceId);
        if (it != synced.innerPlugins.end()) {
            return it->second.get();
        }
    }
    return nullptr;
}

te::Plugin* RackSyncManager::getRackInstance(RackId rackId) const {
    auto it = syncedRacks_.find(rackId);
    if (it != syncedRacks_.end()) {
        return it->second.rackInstance.get();
    }
    return nullptr;
}

bool RackSyncManager::isRackInstance(te::Plugin* plugin) const {
    if (!plugin)
        return false;

    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.rackInstance.get() == plugin) {
            return true;
        }
    }
    return false;
}

RackId RackSyncManager::getRackIdForInstance(te::Plugin* plugin) const {
    if (!plugin)
        return INVALID_RACK_ID;

    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.rackInstance.get() == plugin) {
            return rackId;
        }
    }
    return INVALID_RACK_ID;
}

void RackSyncManager::capturePluginStates(SyncedRack& synced) {
    auto& trackManager = TrackManager::getInstance();

    for (auto& [deviceId, plugin] : synced.innerPlugins) {
        juce::String stateStr;

        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            ext->flushPluginStateToValueTree();
            stateStr = ext->state.getProperty(te::IDs::state).toString();
        } else {
            auto stateCopy = plugin->state.createCopy();
            stateCopy.removeProperty(te::IDs::id, nullptr);
            if (auto xml = stateCopy.createXml())
                stateStr = xml->toString();
        }

        if (auto* devInfo = trackManager.getDevice(synced.trackId, deviceId)) {
            devInfo->pluginState = stateStr;
        }
    }
}

void RackSyncManager::captureAllPluginStates() {
    for (auto& [rackId, synced] : syncedRacks_) {
        capturePluginStates(synced);
    }
}

void RackSyncManager::clear() {
    for (auto& [rackId, synced] : syncedRacks_) {
        if (!synced.rackType)
            continue;

        auto& macroList = synced.rackType->getMacroParameterListForWriting();

        // Remove TE MacroParameters and their modifier assignments
        for (auto& [macroIdx, macroParam] : synced.innerMacroParams) {
            if (!macroParam)
                continue;

            for (auto& [pluginId, plugin] : synced.innerPlugins) {
                if (plugin) {
                    for (auto* param : plugin->getAutomatableParameters())
                        param->removeModifier(*macroParam);
                }
            }

            macroList.removeMacroParameter(*macroParam);
        }
        // Clear LFO callbacks and defer holder destruction
        clearLFOCustomWaveCallbacks(synced.innerModifiers);
        deferCurveSnapshots(synced.curveSnapshots, deferredHolders_);
    }
    syncedRacks_.clear();
    // Note: deferredHolders_ are NOT drained here — this is typically called from
    // PluginManager::clearAllMappings() which drains its own deferred holders after.
    // These will be cleaned up when RackSyncManager is destroyed.
}

void RackSyncManager::setMacroValue(RackId rackId, int macroIndex, float value) {
    auto it = syncedRacks_.find(rackId);
    if (it == syncedRacks_.end())
        return;

    auto& synced = it->second;
    auto macroIt = synced.innerMacroParams.find(macroIndex);
    if (macroIt != synced.innerMacroParams.end() && macroIt->second != nullptr) {
        macroIt->second->setParameter(value, juce::sendNotificationSync);
    }
}

void RackSyncManager::resyncAllModifiers(TrackId trackId) {
    auto& tm = TrackManager::getInstance();
    for (auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;

        // Find the current RackInfo for this rack
        for (const auto& track : tm.getTracks()) {
            if (track.id != trackId)
                continue;
            for (const auto& element : track.chainElements) {
                if (auto* rackPtr = std::get_if<std::unique_ptr<RackInfo>>(&element)) {
                    if (*rackPtr && (*rackPtr)->id == rackId) {
                        syncModifiers(synced, **rackPtr);
                        syncMacros(synced, **rackPtr);
                    }
                }
            }
        }
    }
}

void RackSyncManager::updateAllModifierProperties(TrackId trackId) {
    auto& tm = TrackManager::getInstance();
    for (auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;

        // Find the current RackInfo for this rack
        for (const auto& track : tm.getTracks()) {
            if (track.id != trackId)
                continue;
            for (const auto& element : track.chainElements) {
                if (auto* rackPtr = std::get_if<std::unique_ptr<RackInfo>>(&element)) {
                    if (!*rackPtr || (*rackPtr)->id != rackId)
                        continue;
                    const auto& rackInfo = **rackPtr;

                    // Update existing TE modifiers in-place
                    for (const auto& modInfo : rackInfo.mods) {
                        if (!modInfo.enabled || modInfo.links.empty())
                            continue;

                        auto modIt = synced.innerModifiers.find(modInfo.id);
                        if (modIt == synced.innerModifiers.end() || !modIt->second)
                            continue;

                        auto& modifier = modIt->second;

                        if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                            auto& snapHolder = synced.curveSnapshots[modInfo.id];
                            if (!snapHolder)
                                snapHolder = std::make_unique<CurveSnapshotHolder>();
                            applyLFOProperties(lfo, modInfo, snapHolder.get());
                            if (modInfo.triggered && modInfo.triggerMode != LFOTriggerMode::Free)
                                triggerLFONoteOnWithReset(lfo);
                        }

                        // Update assignment values (mod depth) for each link
                        for (const auto& link : modInfo.links) {
                            if (!link.isValid())
                                continue;

                            auto pluginIt = synced.innerPlugins.find(link.target.deviceId);
                            if (pluginIt == synced.innerPlugins.end() || !pluginIt->second)
                                continue;

                            auto params = pluginIt->second->getAutomatableParameters();
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
                    }
                }
            }
        }
    }
}

// =============================================================================
// Private Implementation
// =============================================================================

void RackSyncManager::loadChainPlugins(SyncedRack& synced, TrackId trackId,
                                       const RackInfo& rackInfo) {
    for (const auto& chain : rackInfo.chains) {
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                auto plugin = createPluginForRack(trackId, device);

                if (plugin) {
                    // Add plugin to the RackType
                    if (synced.rackType->addPlugin(plugin, {0.5f, 0.5f}, false)) {
                        synced.innerPlugins[device.id] = plugin;

                        // Register processor for parameter enumeration
                        pluginManager_.registerRackPluginProcessor(device.id, plugin, device);

                        // Apply bypass state
                        plugin->setEnabled(!device.bypassed);

                        DBG("RackSyncManager: Added plugin '"
                            << device.name << "' (device " << device.id << ") to rack "
                            << synced.rackId << " itemID=" << plugin->itemID.getRawID());
                    } else {
                        DBG("RackSyncManager: Failed to add plugin '" << device.name
                                                                      << "' to rack");
                    }
                }
            }
            // TODO: Handle nested racks (recursive RackInfo in chain elements)
        }

        // Add a VolumeAndPanPlugin for each chain (for per-chain volume/pan)
        auto volPanPlugin =
            edit_.getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::create());
        if (volPanPlugin) {
            if (synced.rackType->addPlugin(volPanPlugin, {0.8f, 0.5f}, false)) {
                synced.chainVolPanPlugins[chain.id] = volPanPlugin;

                // Apply chain volume/pan
                if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(volPanPlugin.get())) {
                    float db = chain.volume;  // Already in dB
                    volPan->setVolumeDb(db);
                    volPan->setPan(chain.pan);
                }
            }
        }
    }
}

void RackSyncManager::buildConnections(SyncedRack& synced, const RackInfo& rackInfo) {
    auto& rackType = synced.rackType;
    auto rackIOId = te::EditItemID();  // Default = rack I/O

    // Determine if any chain is soloed
    bool anySoloed = false;
    for (const auto& chain : rackInfo.chains) {
        if (chain.solo) {
            anySoloed = true;
            break;
        }
    }

    bool anyChainConnectedToOutput = false;

    for (const auto& chain : rackInfo.chains) {
        // Determine if this chain should be active
        bool chainActive = true;
        if (chain.muted)
            chainActive = false;
        if (anySoloed && !chain.solo)
            chainActive = false;

        // Bypassed chain: pass audio straight through to output, skip all plugins
        if (chain.bypassed && chainActive) {
            rackType->addConnection(rackIOId, 1, rackIOId, 1);  // Left passthrough
            rackType->addConnection(rackIOId, 2, rackIOId, 2);  // Right passthrough
            anyChainConnectedToOutput = true;
            continue;
        }

        // Collect device plugins in this chain (in order)
        std::vector<te::EditItemID> chainPluginIds;
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                auto pluginIt = synced.innerPlugins.find(device.id);
                if (pluginIt != synced.innerPlugins.end() && pluginIt->second) {
                    chainPluginIds.push_back(pluginIt->second->itemID);
                }
            }
        }

        // Add the chain's VolumeAndPan plugin at the end (even for empty chains,
        // so they pass clean audio through with per-chain volume/pan control)
        auto volPanIt = synced.chainVolPanPlugins.find(chain.id);
        if (volPanIt != synced.chainVolPanPlugins.end() && volPanIt->second) {
            chainPluginIds.push_back(volPanIt->second->itemID);
        }

        if (chainPluginIds.empty())
            continue;

        // Wire serial connections: rack input → first plugin → ... → last plugin → rack output
        auto firstPlugin = chainPluginIds.front();
        auto lastPlugin = chainPluginIds.back();

        DBG("buildConnections: chain " << chain.id << " has " << chainPluginIds.size()
                                       << " plugins, first=" << firstPlugin.getRawID()
                                       << " last=" << lastPlugin.getRawID()
                                       << " chainActive=" << (int)chainActive);

        // Verify all plugin IDs are recognized by the rack
        for (size_t idx = 0; idx < chainPluginIds.size(); ++idx) {
            auto pid = chainPluginIds[idx];
            bool found = false;
            for (auto* p : rackType->getPlugins()) {
                if (p->itemID == pid) {
                    found = true;
                    break;
                }
            }
            DBG("buildConnections:   plugin[" << idx << "] id=" << pid.getRawID()
                                              << " foundInRack=" << (int)found);
        }

        // Connect rack audio input to first plugin (L/R)
        rackType->addConnection(rackIOId, 1, firstPlugin, 1);
        rackType->addConnection(rackIOId, 2, firstPlugin, 2);

        // Connect rack MIDI input to first plugin
        rackType->addConnection(rackIOId, 0, firstPlugin, 0);

        // Serial connections between consecutive plugins
        for (size_t i = 0; i + 1 < chainPluginIds.size(); ++i) {
            auto src = chainPluginIds[i];
            auto dst = chainPluginIds[i + 1];
            rackType->addConnection(src, 1, dst, 1);  // Left
            rackType->addConnection(src, 2, dst, 2);  // Right
            rackType->addConnection(src, 0, dst, 0);  // MIDI
        }

        // Connect last plugin to rack output.
        // Audio L/R is only wired when the chain is active (mute/solo gating),
        // but MIDI pin 0 is wired unconditionally so a MIDI plugin at the end
        // of the chain can still feed downstream MIDI plugins / instruments
        // when the chain is bypassed — matching TE's standard bypass semantics
        // (a bypassed plugin passes MIDI through unchanged).
        rackType->addConnection(lastPlugin, 0, rackIOId, 0);  // MIDI — always
        if (chainActive) {
            rackType->addConnection(lastPlugin, 1, rackIOId, 1);  // Left
            rackType->addConnection(lastPlugin, 2, rackIOId, 2);  // Right
            anyChainConnectedToOutput = true;
        }
    }

    // If no chains exist at all, pass audio and MIDI straight through so the
    // rack is transparent. When chains exist but are all muted/inactive, leave
    // audio disconnected for silence (MIDI is already wired per-chain above).
    if (!anyChainConnectedToOutput && rackInfo.chains.empty()) {
        rackType->addConnection(rackIOId, 1, rackIOId, 1);  // Audio L
        rackType->addConnection(rackIOId, 2, rackIOId, 2);  // Audio R
        rackType->addConnection(rackIOId, 0, rackIOId, 0);  // MIDI
    }
}

te::Plugin::Ptr RackSyncManager::createPluginForRack(TrackId trackId, const DeviceInfo& device) {
    return pluginManager_.createPluginOnly(trackId, device);
}

bool RackSyncManager::structureChanged(const SyncedRack& synced, const RackInfo& rackInfo) const {
    // Check if number of chains changed
    if (synced.chainVolPanPlugins.size() != rackInfo.chains.size())
        return true;

    // Check if the set of devices or their order changed
    for (const auto& chain : rackInfo.chains) {
        // Check chain still exists
        if (synced.chainVolPanPlugins.find(chain.id) == synced.chainVolPanPlugins.end())
            return true;

        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                if (synced.innerPlugins.find(device.id) == synced.innerPlugins.end())
                    return true;
            }
        }
    }

    // Check if any synced device no longer exists in the rack
    for (const auto& [deviceId, plugin] : synced.innerPlugins) {
        bool found = false;
        for (const auto& chain : rackInfo.chains) {
            for (const auto& element : chain.elements) {
                if (isDevice(element) && getDevice(element).id == deviceId) {
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
            return true;
    }

    return false;
}

void RackSyncManager::updateProperties(SyncedRack& synced, const RackInfo& rackInfo) {
    // Update rack bypass state
    applyBypassState(synced, rackInfo);

    // Update per-chain volume/pan
    for (const auto& chain : rackInfo.chains) {
        auto volPanIt = synced.chainVolPanPlugins.find(chain.id);
        if (volPanIt != synced.chainVolPanPlugins.end() && volPanIt->second) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(volPanIt->second.get())) {
                volPan->setVolumeDb(chain.volume);
                volPan->setPan(chain.pan);
            }
        }
    }

    // Check if mute/solo state requires connection rebuild
    // (We need to compare against current connection state, but for simplicity
    // we always rebuild connections — this is cheap compared to recreating plugins)
    {
        auto& rackType = synced.rackType;
        if (rackType) {
            auto connections = rackType->getConnections();
            for (int i = connections.size(); --i >= 0;) {
                auto* conn = connections[i];
                rackType->removeConnection(conn->sourceID, conn->sourcePin, conn->destID,
                                           conn->destPin);
            }
            buildConnections(synced, rackInfo);
        }
    }

    // Update individual device bypass states
    for (const auto& chain : rackInfo.chains) {
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                auto pluginIt = synced.innerPlugins.find(device.id);
                if (pluginIt != synced.innerPlugins.end() && pluginIt->second) {
                    pluginIt->second->setEnabled(!device.bypassed);
                }
            }
        }
    }

    // Resync modifiers and macros (lightweight — just rebuilds TE modifier
    // assignments, no plugin state is lost)
    syncModifiers(synced, rackInfo);
    syncMacros(synced, rackInfo);

    DBG("RackSyncManager: Updated properties for rack " << rackInfo.id);
}

void RackSyncManager::applyBypassState(SyncedRack& synced, const RackInfo& rackInfo) {
    if (!synced.rackInstance)
        return;

    auto* rackInstance = dynamic_cast<te::RackInstance*>(synced.rackInstance.get());
    if (!rackInstance)
        return;

    if (rackInfo.bypassed) {
        // Bypass: dry signal only
        rackInstance->wetGain->setParameter(0.0f, juce::dontSendNotification);
        rackInstance->dryGain->setParameter(1.0f, juce::dontSendNotification);
    } else {
        // Normal: wet signal only (processed through rack)
        rackInstance->wetGain->setParameter(1.0f, juce::dontSendNotification);
        rackInstance->dryGain->setParameter(0.0f, juce::dontSendNotification);
    }

    // Apply rack output volume/pan via the RackInstance's output level parameters
    rackInstance->leftOutDb->setParameter(
        static_cast<float>(juce::jlimit(te::RackInstance::rackMinDb, te::RackInstance::rackMaxDb,
                                        static_cast<double>(rackInfo.volume))),
        juce::dontSendNotification);
    rackInstance->rightOutDb->setParameter(
        static_cast<float>(juce::jlimit(te::RackInstance::rackMinDb, te::RackInstance::rackMaxDb,
                                        static_cast<double>(rackInfo.volume))),
        juce::dontSendNotification);
}

// =============================================================================
// Phase 2: Modifiers & Macros
// =============================================================================

void RackSyncManager::syncModifiers(SyncedRack& synced, const RackInfo& rackInfo) {
    auto& rackType = synced.rackType;
    if (!rackType)
        return;

    auto& modList = rackType->getModifierList();

    // Remove existing TE modifiers before recreating
    // Clear LFO callbacks before destroying CurveSnapshotHolders
    clearLFOCustomWaveCallbacks(synced.innerModifiers);

    for (auto& [modId, mod] : synced.innerModifiers) {
        if (!mod)
            continue;

        // Remove modifier assignments from all inner plugin parameters
        for (auto& [pluginId, plugin] : synced.innerPlugins) {
            if (plugin) {
                for (auto* param : plugin->getAutomatableParameters())
                    param->removeModifier(*mod);
            }
        }

        modList.state.removeChild(mod->state, nullptr);
    }
    synced.innerModifiers.clear();

    for (const auto& modInfo : rackInfo.mods) {
        if (!modInfo.enabled || modInfo.links.empty())
            continue;

        te::Modifier::Ptr modifier;

        switch (modInfo.type) {
            case ModType::LFO: {
                // Create LFO modifier via ValueTree
                juce::ValueTree lfoState(te::IDs::LFO);
                auto lfoMod = modList.insertModifier(lfoState, -1, nullptr);
                if (!lfoMod)
                    break;

                if (auto* lfo = dynamic_cast<te::LFOModifier*>(lfoMod.get())) {
                    auto& snapHolder = synced.curveSnapshots[modInfo.id];
                    if (!snapHolder)
                        snapHolder = std::make_unique<CurveSnapshotHolder>();
                    applyLFOProperties(lfo, modInfo, snapHolder.get());
                }
                modifier = lfoMod;
                DBG("RackSyncManager::syncModifiers - created LFO for rackId="
                    << synced.rackId << " modId=" << modInfo.id << " triggerMode="
                    << (int)modInfo.triggerMode << " links=" << (int)modInfo.links.size());
                break;
            }

            case ModType::Random: {
                juce::ValueTree randomState(te::IDs::RANDOM);
                auto randomMod = modList.insertModifier(randomState, -1, nullptr);
                modifier = randomMod;
                break;
            }

            case ModType::Follower: {
                juce::ValueTree envState(te::IDs::ENVELOPEFOLLOWER);
                auto envMod = modList.insertModifier(envState, -1, nullptr);
                modifier = envMod;
                break;
            }

            case ModType::Envelope: {
                // TE doesn't have a direct envelope generator — use LFO one-shot as approximation
                // For now, skip Envelope type
                break;
            }
        }

        if (!modifier)
            continue;

        synced.innerModifiers[modInfo.id] = modifier;

        // Create modifier assignments for each link
        for (const auto& link : modInfo.links) {
            if (!link.isValid())
                continue;

            auto pluginIt = synced.innerPlugins.find(link.target.deviceId);
            if (pluginIt == synced.innerPlugins.end() || !pluginIt->second)
                continue;

            auto params = pluginIt->second->getAutomatableParameters();
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

void RackSyncManager::syncMacros(SyncedRack& synced, const RackInfo& rackInfo) {
    auto& rackType = synced.rackType;
    if (!rackType)
        return;

    auto& macroList = rackType->getMacroParameterListForWriting();

    // Remove existing TE MacroParameters before recreating
    for (auto& [macroIdx, macroParam] : synced.innerMacroParams) {
        if (!macroParam)
            continue;

        // Remove modifier assignments from all inner plugin parameters
        for (auto& [pluginId, plugin] : synced.innerPlugins) {
            if (plugin) {
                for (auto* param : plugin->getAutomatableParameters())
                    param->removeModifier(*macroParam);
            }
        }

        macroList.removeMacroParameter(*macroParam);
    }
    synced.innerMacroParams.clear();

    for (int i = 0; i < static_cast<int>(rackInfo.macros.size()); ++i) {
        const auto& macroInfo = rackInfo.macros[static_cast<size_t>(i)];
        if (!macroInfo.isLinked())
            continue;

        // Create a TE MacroParameter
        auto* macroParam = macroList.createMacroParameter();
        if (!macroParam)
            continue;

        macroParam->macroName = macroInfo.name;
        macroParam->setParameter(macroInfo.value, juce::dontSendNotification);

        synced.innerMacroParams[i] = macroParam;

        // Create assignments for each link
        for (const auto& link : macroInfo.links) {
            if (!link.target.isValid())
                continue;

            auto pluginIt = synced.innerPlugins.find(link.target.deviceId);
            if (pluginIt == synced.innerPlugins.end() || !pluginIt->second)
                continue;

            auto params = pluginIt->second->getAutomatableParameters();
            if (link.target.paramIndex >= 0 &&
                link.target.paramIndex < static_cast<int>(params.size())) {
                auto* param = params[static_cast<size_t>(link.target.paramIndex)];
                if (param) {
                    param->addModifier(*macroParam, link.amount);
                    DBG("RackSyncManager: Linked macro " << i << " to device "
                                                         << link.target.deviceId << " param "
                                                         << link.target.paramIndex);
                }
            }
        }

        // Also handle legacy single target
        if (macroInfo.target.isValid()) {
            auto pluginIt = synced.innerPlugins.find(macroInfo.target.deviceId);
            if (pluginIt != synced.innerPlugins.end() && pluginIt->second) {
                auto params = pluginIt->second->getAutomatableParameters();
                if (macroInfo.target.paramIndex >= 0 &&
                    macroInfo.target.paramIndex < static_cast<int>(params.size())) {
                    auto* param = params[static_cast<size_t>(macroInfo.target.paramIndex)];
                    if (param) {
                        param->addModifier(*macroParam, 1.0f);
                    }
                }
            }
        }
    }
}

bool RackSyncManager::needsModifierResync(TrackId trackId) const {
    auto& tm = TrackManager::getInstance();
    auto* trackInfo = tm.getTrack(trackId);
    if (!trackInfo)
        return false;

    for (const auto& element : trackInfo->chainElements) {
        if (!isRack(element))
            continue;

        const auto& rack = getRack(element);
        auto it = syncedRacks_.find(rack.id);
        if (it == syncedRacks_.end())
            continue;

        const auto& synced = it->second;

        // Count active rack mods (enabled + has links)
        int activeModCount = 0;
        for (const auto& mod : rack.mods) {
            if (mod.enabled && !mod.links.empty())
                activeModCount++;
        }

        int existingCount = static_cast<int>(synced.innerModifiers.size());
        if (activeModCount != existingCount)
            return true;
    }

    return false;
}

void RackSyncManager::collectLFOModifiers(TrackId trackId,
                                          std::vector<te::LFOModifier*>& out) const {
    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;
        int collected = 0;
        for (const auto& [modId, modifier] : synced.innerModifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                out.push_back(lfo);
                ++collected;
            }
        }
        if (collected > 0)
            DBG("RackSyncManager::collectLFOModifiers - rackId=" << rackId << " trackId=" << trackId
                                                                 << " collected=" << collected);
    }
}

void RackSyncManager::triggerLFONoteOn(TrackId trackId) {
    for (auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;

        for (auto& [modId, modifier] : synced.innerModifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                triggerLFONoteOnWithReset(lfo);
            }
        }
    }
}

}  // namespace magda
