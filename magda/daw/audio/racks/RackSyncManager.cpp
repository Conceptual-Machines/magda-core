#include "racks/RackSyncManager.hpp"

#include "TracktionHelpers.hpp"
#include "core/ChainRoutingModel.hpp"
#include "core/TrackManager.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "modifiers/ModifierHelpers.hpp"
#include "modifiers/ModifierSync.hpp"
#include "plugin_manager/PluginManager.hpp"

namespace magda {

namespace {

juce::String pathKey(const ChainNodePath& path) {
    return path.toString();
}

std::pair<float, float> outputDbForVolumePan(float volumeDb, float pan) {
    const auto limitedVolume = static_cast<float>(juce::jlimit(
        te::RackInstance::rackMinDb, te::RackInstance::rackMaxDb, static_cast<double>(volumeDb)));
    const auto limitedPan = juce::jlimit(-1.0f, 1.0f, pan);
    const auto leftGain = limitedPan > 0.0f ? 1.0f - limitedPan : 1.0f;
    const auto rightGain = limitedPan < 0.0f ? 1.0f + limitedPan : 1.0f;
    const auto leftDb = limitedVolume + juce::Decibels::gainToDecibels(leftGain);
    const auto rightDb = limitedVolume + juce::Decibels::gainToDecibels(rightGain);
    return {
        static_cast<float>(juce::jlimit(te::RackInstance::rackMinDb, te::RackInstance::rackMaxDb,
                                        static_cast<double>(leftDb))),
        static_cast<float>(juce::jlimit(te::RackInstance::rackMinDb, te::RackInstance::rackMaxDb,
                                        static_cast<double>(rightDb)))};
}

void applyRackInstanceState(te::Plugin::Ptr rackPlugin, const RackInfo& rackInfo) {
    auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
    if (!rackInstance)
        return;

    if (rackInfo.bypassed) {
        rackInstance->wetGain->setParameterFromHost(0.0f, juce::dontSendNotification);
        rackInstance->dryGain->setParameterFromHost(1.0f, juce::dontSendNotification);
    } else {
        rackInstance->wetGain->setParameterFromHost(1.0f, juce::dontSendNotification);
        rackInstance->dryGain->setParameterFromHost(0.0f, juce::dontSendNotification);
    }

    auto [leftDb, rightDb] = outputDbForVolumePan(rackInfo.volume, rackInfo.pan);
    rackInstance->leftOutDb->setParameterFromHost(leftDb, juce::dontSendNotification);
    rackInstance->rightOutDb->setParameterFromHost(rightDb, juce::dontSendNotification);
}

void ensureRackOutputPair(te::RackType& rackType, int outputIndex) {
    const auto clampedIndex = juce::jlimit(0, 31, outputIndex);
    const auto rightPin = 2 + (clampedIndex * 2);

    while (rackType.getOutputNames().size() <= rightPin) {
        const auto nextPin = rackType.getOutputNames().size();
        rackType.addOutput(-1, "output " + juce::String(nextPin));
    }
}

te::Plugin* findRackPlugin(te::RackType& rackType, te::EditItemID id) {
    for (auto* plugin : rackType.getPlugins()) {
        if (plugin && plugin->itemID == id)
            return plugin;
    }
    return nullptr;
}

int getAudioInputCount(te::RackType& rackType, te::EditItemID id) {
    if (auto* plugin = findRackPlugin(rackType, id)) {
        juce::StringArray inputs;
        plugin->getChannelNames(&inputs, nullptr);
        return inputs.size();
    }
    return 2;
}

int getAudioOutputCount(te::RackType& rackType, te::EditItemID id) {
    if (auto* plugin = findRackPlugin(rackType, id)) {
        juce::StringArray outputs;
        plugin->getChannelNames(nullptr, &outputs);
        return outputs.size();
    }
    return 2;
}

bool addRackConnection(te::RackType& rackType, te::EditItemID src, int sourcePin,
                       te::EditItemID dst, int destPin, const juce::String&) {
    const auto ok = rackType.addConnection(src, sourcePin, dst, destPin);
    return ok;
}

struct ChainPluginNode {
    te::EditItemID id;
    bool isInstrument = false;
    bool outputsPluginMidi = false;
    bool receivesChainMidi = true;
    bool passesRawMidiInput = false;
};

struct AudioBusSource {
    te::EditItemID id;
    int leftPin = 1;
    int rightPin = 2;
};

}  // namespace

RackSyncManager::RackSyncManager(te::Edit& edit, PluginManager& pluginManager)
    : edit_(edit), pluginManager_(pluginManager) {}

// =============================================================================
// Public API
// =============================================================================

te::Plugin::Ptr RackSyncManager::syncRack(TrackId trackId, const RackInfo& rackInfo) {
    deferredHolders_.clear();  // Drain previous cycle's deferred holders

    // Check if already synced
    auto it = syncedRacks_.find(rackInfo.id);
    if (it != syncedRacks_.end()) {
        bool changed = structureChanged(it->second, rackInfo);
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
    syncRackModulation(synced, rackInfo);

    // 6. Create a RackInstance from the RackType
    auto rackInstanceState = te::RackInstance::create(*rackType);
    auto rackInstance = edit_.getPluginCache().createNewPlugin(rackInstanceState);

    if (!rackInstance) {
        edit_.getRackList().removeRackType(rackType);
        return nullptr;
    }

    synced.rackInstance = rackInstance;

    // 7. Apply bypass state
    applyBypassState(synced, rackInfo);

    // 8. Store synced state
    syncedRacks_[rackInfo.id] = std::move(synced);

    // Seed the structural fingerprint so resyncAllModifiers can short-circuit
    // when only properties change.
    rackFingerprints_[rackInfo.id] = computeRackFingerprint(rackInfo);

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

    for (auto& [_chainKey, plugin] : synced.chainVolPanPlugins) {
        if (plugin)
            plugin->deleteFromParent();
    }
    synced.chainVolPanPlugins.clear();
    clearNestedRackState(synced);

    // Reload chain plugins and rebuild connections
    loadChainPlugins(synced, trackId, rackInfo);
    buildConnections(synced, rackInfo);

    // Resync modifiers and macros
    syncRackModulation(synced, rackInfo);

    // Reapply bypass
    applyBypassState(synced, rackInfo);

    // Refresh the stored fingerprint after a structural rebuild.
    rackFingerprints_[rackInfo.id] = computeRackFingerprint(rackInfo);
}

void RackSyncManager::updateRackProperties(const RackInfo& rackInfo) {
    auto it = syncedRacks_.find(rackInfo.id);
    if (it == syncedRacks_.end())
        return;
    updateProperties(it->second, rackInfo);
}

void RackSyncManager::removeRack(RackId rackId) {
    removeRackInternal(rackId, true);
}

void RackSyncManager::removeRackForMove(RackId rackId) {
    removeRackInternal(rackId, false);
}

void RackSyncManager::removeRackInternal(RackId rackId, bool clearDeviceState) {
    auto it = syncedRacks_.find(rackId);
    if (it == syncedRacks_.end())
        return;

    auto& synced = it->second;

    if (clearDeviceState) {
        // Clear saved plugin state so re-adding the same device gets fresh defaults
        auto& trackManager = TrackManager::getInstance();
        for (auto& [deviceId, plugin] : synced.innerPlugins) {
            if (auto* devInfo = trackManager.getDevice(synced.trackId, deviceId)) {
                devInfo->pluginState.clear();
            } else {
            }
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
    for (auto& [_path, state] : synced.nestedRackMods) {
        clearLFOCustomWaveCallbacks(state.modifiers);
        deferCurveSnapshots(state.curveSnapshots, deferredHolders_);
        for (auto& [_deviceId, devState] : state.innerDeviceMods) {
            clearLFOCustomWaveCallbacks(devState.modifiers);
            deferCurveSnapshots(devState.curveSnapshots, deferredHolders_);
        }
    }
    clearNestedRackState(synced);

    syncedRacks_.erase(it);
    rackFingerprints_.erase(rackId);
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
    auto& tm = TrackManager::getInstance();

    for (const auto& [rackId, synced] : syncedRacks_) {
        auto& info = map[synced.trackId];
        info.rackIds.push_back(rackId);

        auto* rackInfo = tm.getRack(synced.trackId, rackId);
        if (rackInfo == nullptr)
            continue;

        std::function<void(const RackInfo&, const ChainNodePath&)> collectPaths;
        collectPaths = [&](const RackInfo& rack, const ChainNodePath& rackPath) {
            for (const auto& chain : rack.chains) {
                const auto chainPath = rackPath.withChain(chain.id);
                for (const auto& element : chain.elements) {
                    if (isDevice(element)) {
                        const auto& device = getDevice(element);
                        auto pluginIt = synced.innerPlugins.find(device.id);
                        if (pluginIt != synced.innerPlugins.end() && pluginIt->second)
                            info.devicePaths.push_back(chainPath.withDevice(device.id));
                    } else if (isRack(element)) {
                        const auto& nestedRack = getRack(element);
                        collectPaths(nestedRack, chainPath.withRack(nestedRack.id));
                    }
                }
            }
        };
        collectPaths(*rackInfo, ChainNodePath::rack(synced.trackId, rackId));
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

void RackSyncManager::syncSidechains(
    const RackInfo& rackInfo,
    const std::function<te::AudioTrack*(TrackId sourceTrackId)>& resolveSourceTrack) {
    auto syncedIt = syncedRacks_.find(rackInfo.id);
    if (syncedIt == syncedRacks_.end())
        return;

    auto& synced = syncedIt->second;

    std::function<void(const RackInfo&)> syncRack = [&](const RackInfo& rack) {
        for (const auto& chain : rack.chains) {
            for (const auto& element : chain.elements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    auto pluginIt = synced.innerPlugins.find(device.id);
                    if (pluginIt == synced.innerPlugins.end() || !pluginIt->second)
                        continue;

                    auto* plugin = pluginIt->second.get();
                    if (!plugin->canSidechain())
                        continue;

                    if (device.sidechain.isActive() &&
                        device.sidechain.type == SidechainConfig::Type::Audio) {
                        if (auto* sourceTrack =
                                resolveSourceTrack(device.sidechain.sourceTrackId)) {
                            plugin->setSidechainSourceID(sourceTrack->itemID);
                            plugin->guessSidechainRouting();
                            continue;
                        }
                    }

                    plugin->setSidechainSourceID({});
                } else if (isRack(element)) {
                    syncRack(getRack(element));
                }
            }
        }
    };

    syncRack(rackInfo);
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

    // Resolve devices through the rack's chain hierarchy rather than
    // TrackManager::getDevice, which only finds top-level devices and would
    // return nullptr for anything inside a rack — silently dropping the
    // captured state and resetting the plugin to defaults on resync.
    auto* rackInfo = trackManager.getRack(synced.trackId, synced.rackId);
    if (rackInfo == nullptr)
        return;

    std::function<bool(std::vector<ChainElement>&, const ChainNodePath&, DeviceId, DeviceInfo*&,
                       ChainNodePath&)>
        findInChains;
    findInChains = [&](std::vector<ChainElement>& elements, const ChainNodePath& parentChainPath,
                       DeviceId id, DeviceInfo*& outDevice, ChainNodePath& outPath) -> bool {
        for (auto& element : elements) {
            if (isDevice(element)) {
                auto& dev = getDevice(element);
                if (dev.id == id) {
                    outDevice = &dev;
                    outPath = parentChainPath.withDevice(id);
                    return true;
                }
            } else if (isRack(element)) {
                auto& nestedRack = getRack(element);
                const auto nestedRackPath = parentChainPath.withRack(nestedRack.id);
                for (auto& chain : nestedRack.chains) {
                    if (findInChains(chain.elements, nestedRackPath.withChain(chain.id), id,
                                     outDevice, outPath))
                        return true;
                }
            }
        }
        return false;
    };

    for (auto& [deviceId, plugin] : synced.innerPlugins) {
        juce::String stateStr;

        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            ext->flushPluginStateToValueTree();
            stateStr = ext->state.getProperty(te::IDs::state).toString();
        } else {
            plugin->flushPluginStateToValueTree();
            // Strip MODIFIERASSIGNMENTS so the post-restore syncModifiers
            // adds fresh assignments instead of doubling up on the
            // captured ones (which would re-apply the LFO modulation on
            // top of the already-modulated value, sweeping params past
            // their range).
            auto stateCopy = plugin->state.createCopy();
            stripTracktionIdsRecursive(stateCopy);
            stripModifierAssignmentsRecursive(stateCopy);
            if (auto xml = stateCopy.createXml())
                stateStr = xml->toString();
        }

        const auto rackPath = ChainNodePath::rack(synced.trackId, synced.rackId);
        for (auto& chain : rackInfo->chains) {
            DeviceInfo* devInfo = nullptr;
            ChainNodePath devicePath;
            if (findInChains(chain.elements, rackPath.withChain(chain.id), deviceId, devInfo,
                             devicePath)) {
                devInfo->pluginState = stateStr;
                pluginManager_.refreshDeviceParameters(devicePath);
                break;
            }
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
        for (auto& [_path, state] : synced.nestedRackMods) {
            clearLFOCustomWaveCallbacks(state.modifiers);
            deferCurveSnapshots(state.curveSnapshots, deferredHolders_);
            for (auto& [_deviceId, devState] : state.innerDeviceMods) {
                clearLFOCustomWaveCallbacks(devState.modifiers);
                deferCurveSnapshots(devState.curveSnapshots, deferredHolders_);
            }
        }
        clearNestedRackState(synced);
    }
    syncedRacks_.clear();
    // Note: deferredHolders_ are NOT drained here — this is typically called from
    // PluginManager::clearAllMappings() which drains its own deferred holders after.
    // These will be cleaned up when RackSyncManager is destroyed.
}

void RackSyncManager::clearNestedRackState(SyncedRack& synced) {
    for (auto& [_path, rackInstance] : synced.nestedRackInstances) {
        if (rackInstance)
            rackInstance->deleteFromParent();
    }
    synced.nestedRackInstances.clear();

    for (auto& [_path, rackType] : synced.nestedRackTypes) {
        if (rackType)
            edit_.getRackList().removeRackType(rackType);
    }
    synced.nestedRackTypes.clear();

    for (auto& [_path, state] : synced.nestedRackMods) {
        clearLFOCustomWaveCallbacks(state.modifiers);
        deferCurveSnapshots(state.curveSnapshots, deferredHolders_);
        for (auto& [_deviceId, devState] : state.innerDeviceMods) {
            clearLFOCustomWaveCallbacks(devState.modifiers);
            deferCurveSnapshots(devState.curveSnapshots, deferredHolders_);
        }
    }
    synced.nestedRackMods.clear();
}

void RackSyncManager::setMacroValue(RackId rackId, int macroIndex, float value) {
    auto it = syncedRacks_.find(rackId);
    if (it == syncedRacks_.end())
        return;

    auto& synced = it->second;
    auto macroIt = synced.innerMacroParams.find(macroIndex);
    if (macroIt != synced.innerMacroParams.end() && macroIt->second != nullptr) {
        macroIt->second->setParameterFromHost(value, juce::sendNotificationSync);
    }
}

te::AutomatableParameter* RackSyncManager::findRackMacroParameter(RackId rackId,
                                                                  int macroIndex) const {
    auto it = syncedRacks_.find(rackId);
    if (it == syncedRacks_.end())
        return nullptr;
    auto macroIt = it->second.innerMacroParams.find(macroIndex);
    if (macroIt == it->second.innerMacroParams.end() || macroIt->second == nullptr)
        return nullptr;
    return macroIt->second;
}

te::AutomatableParameter* RackSyncManager::findRackModifierParameter(RackId rackId, ModId modId,
                                                                     int paramIndex) const {
    // Single Rate lane (paramIndex 0) maps to either TE's `rate` or `rateType`
    // based on the modifier's tempoSync flag. Future depth lane lives at 1.
    if (paramIndex < 0 || paramIndex >= 2)
        return nullptr;

    auto rackIt = syncedRacks_.find(rackId);
    if (rackIt == syncedRacks_.end())
        return nullptr;
    auto modIt = rackIt->second.innerModifiers.find(modId);
    if (modIt == rackIt->second.innerModifiers.end() || !modIt->second)
        return nullptr;

    // Look up the MAGDA-side ModInfo to read tempoSync. Same scope walk the
    // rest of RackSyncManager uses to reach the rack's mods vector.
    bool sync = false;
    auto& tm = TrackManager::getInstance();
    auto resolved = tm.resolvePath(ChainNodePath::rack(rackIt->second.trackId, rackId));
    if (resolved.valid && resolved.rack) {
        for (const auto& m : resolved.rack->mods) {
            if (m.id == modId) {
                sync = m.tempoSync;
                break;
            }
        }
    }
    const juce::String wantedID = paramIndex == 0 ? (sync ? "rateType" : "rate") : "depth";

    for (auto* p : modIt->second->getAutomatableParameters()) {
        if (p && p->paramID == wantedID)
            return p;
    }
    return nullptr;
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
            for (const auto& element : track.chain.fxChainElements) {
                if (auto* rackPtr = std::get_if<std::unique_ptr<RackInfo>>(&element)) {
                    if (!*rackPtr || (*rackPtr)->id != rackId)
                        continue;
                    const auto& rackInfo = **rackPtr;

                    // Gate on structural fingerprint: amount-only edits skip
                    // the full TE-modifier teardown+rebuild and take the
                    // in-place properties path. Without this, every macro-
                    // link drag would destroy and recreate every rack LFO on
                    // the audio thread (audible clicks; risk of TE
                    // AutomationSourceList tearing down a CachedSource that
                    // the audio thread is still reading). The fingerprint
                    // covers both rack-scope mods AND mods on devices inside
                    // the rack chain — both live on the rackType modifier
                    // list and both need the structural rebuild path when
                    // their shape changes.
                    auto current = computeRackFingerprint(rackInfo);
                    auto& stored = rackFingerprints_[rackId];
                    if (current != stored) {
                        stored = current;
                        syncRackModulation(synced, rackInfo);
                    } else {
                        updateRackModulationProperties(synced, rackInfo);
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
        for (const auto& track : tm.getTracks()) {
            if (track.id != trackId)
                continue;
            for (const auto& element : track.chain.fxChainElements) {
                if (auto* rackPtr = std::get_if<std::unique_ptr<RackInfo>>(&element)) {
                    if (!*rackPtr || (*rackPtr)->id != rackId)
                        continue;
                    updateRackModulationProperties(synced, **rackPtr);
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
    loadRackContents(synced, trackId, rackInfo, ChainNodePath::rack(trackId, rackInfo.id),
                     *synced.rackType);
}

void RackSyncManager::loadRackContents(SyncedRack& synced, TrackId trackId,
                                       const RackInfo& rackInfo, const ChainNodePath& rackPath,
                                       te::RackType& rackType) {
    for (const auto& chain : rackInfo.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                auto plugin = createPluginForRack(trackId, device);

                if (plugin) {
                    // Add plugin to the RackType
                    if (rackType.addPlugin(plugin, {0.5f, 0.5f}, false)) {
                        synced.innerPlugins[device.id] = plugin;

                        // Register processor for parameter enumeration
                        pluginManager_.registerRackPluginProcessor(chainPath.withDevice(device.id),
                                                                   plugin, device);

                        // Apply bypass state
                        plugin->setEnabled(!device.bypassed);

                    } else {
                    }
                }
            } else if (isRack(element)) {
                const auto& nestedRack = getRack(element);
                const auto nestedRackPath = chainPath.withRack(nestedRack.id);
                const auto nestedKey = pathKey(nestedRackPath);

                auto nestedType = edit_.getRackList().addNewRack();
                if (!nestedType) {
                    continue;
                }
                nestedType->rackName =
                    nestedRack.name.isNotEmpty() ? nestedRack.name : "Nested FX Rack";

                loadRackContents(synced, trackId, nestedRack, nestedRackPath, *nestedType);
                buildConnectionsForRack(synced, nestedRack, nestedRackPath, *nestedType);
                syncRackModulationRecursive(synced, nestedRack, nestedRackPath, *nestedType);

                auto nestedInstanceState = te::RackInstance::create(*nestedType);
                auto nestedInstance = edit_.getPluginCache().createNewPlugin(nestedInstanceState);
                if (!nestedInstance) {
                    edit_.getRackList().removeRackType(nestedType);
                    continue;
                }

                if (rackType.addPlugin(nestedInstance, {0.65f, 0.5f}, false)) {
                    applyRackInstanceState(nestedInstance, nestedRack);
                    synced.nestedRackTypes[nestedKey] = nestedType;
                    synced.nestedRackInstances[nestedKey] = nestedInstance;
                } else {
                    nestedInstance->deleteFromParent();
                    edit_.getRackList().removeRackType(nestedType);
                }
            }
        }

        // Add a VolumeAndPanPlugin for each chain (for per-chain volume/pan)
        auto volPanPlugin =
            edit_.getPluginCache().createNewPlugin(te::VolumeAndPanPlugin::create());
        if (volPanPlugin) {
            if (rackType.addPlugin(volPanPlugin, {0.8f, 0.5f}, false)) {
                synced.chainVolPanPlugins[pathKey(chainPath)] = volPanPlugin;

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
    buildConnectionsForRack(synced, rackInfo, ChainNodePath::rack(synced.trackId, rackInfo.id),
                            *synced.rackType);
}

void RackSyncManager::buildConnectionsForRack(SyncedRack& synced, const RackInfo& rackInfo,
                                              const ChainNodePath& rackPath,
                                              te::RackType& rackType) {
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
        const auto chainPath = rackPath.withChain(chain.id);
        ensureRackOutputPair(rackType, chain.outputIndex);
        const auto outLeftPin = 1 + (juce::jlimit(0, 31, chain.outputIndex) * 2);
        const auto outRightPin = outLeftPin + 1;

        // Determine if this chain should be active
        bool chainActive = true;
        if (chain.muted)
            chainActive = false;
        if (anySoloed && !chain.solo)
            chainActive = false;

        // Bypassed chain: pass audio straight through to output, skip all plugins
        if (chain.bypassed && chainActive) {
            rackType.addConnection(rackIOId, 1, rackIOId, outLeftPin);   // Left passthrough
            rackType.addConnection(rackIOId, 2, rackIOId, outRightPin);  // Right passthrough
            rackType.addConnection(rackIOId, 0, rackIOId, 0);            // MIDI passthrough
            anyChainConnectedToOutput = true;
            continue;
        }

        // Collect device plugins in this chain (in order)
        std::vector<ChainPluginNode> chainPluginNodes;
        const auto routingPlan =
            routing::compileRackChainRouting(synced.trackId, rackInfo.id, chain);

        for (const auto& node : routingPlan.nodes) {
            if (node.kind == routing::ChainRoutingNodeKind::Device) {
                auto pluginIt = synced.innerPlugins.find(node.deviceId);
                if (pluginIt != synced.innerPlugins.end() && pluginIt->second) {
                    chainPluginNodes.push_back({pluginIt->second->itemID, node.injectsAudio(),
                                                node.outputsPluginMidi(), node.receivesChainMidi(),
                                                node.passesRawMidiInput()});
                }
            } else if (node.kind == routing::ChainRoutingNodeKind::Rack) {
                const auto nestedKey = pathKey(chainPath.withRack(node.rackId));
                auto rackIt = synced.nestedRackInstances.find(nestedKey);
                if (rackIt != synced.nestedRackInstances.end() && rackIt->second) {
                    chainPluginNodes.push_back({rackIt->second->itemID, false,
                                                node.outputsPluginMidi(), node.receivesChainMidi(),
                                                node.passesRawMidiInput()});
                }
            }
        }

        // Add the chain's VolumeAndPan plugin at the end (even for empty chains,
        // so they pass clean audio through with per-chain volume/pan control)
        auto volPanIt = synced.chainVolPanPlugins.find(pathKey(chainPath));
        if (volPanIt != synced.chainVolPanPlugins.end() && volPanIt->second) {
            chainPluginNodes.push_back({volPanIt->second->itemID, false, false, false, false});
        }

        if (chainPluginNodes.empty())
            continue;

        std::vector<te::EditItemID> chainPluginIds;
        chainPluginIds.reserve(chainPluginNodes.size());
        for (const auto& node : chainPluginNodes)
            chainPluginIds.push_back(node.id);

        // Wire an explicit chain audio bus and MIDI bus:
        // - effects/nested racks process the current audio bus
        // - instruments inject generated audio into the current audio bus
        // - MIDI processors can replace, pass through, or merge with the MIDI bus
        // - instruments and MIDI-triggered FX receive MIDI without stopping it
        std::vector<AudioBusSource> audioBusSources = {{rackIOId, 1, 2}};
        std::vector<te::EditItemID> midiBusSources = {rackIOId};

        for (const auto& node : chainPluginNodes) {
            const auto inputChannels = getAudioInputCount(rackType, node.id);
            const auto outputChannels = getAudioOutputCount(rackType, node.id);

            if (node.receivesChainMidi) {
                const auto inputMidiSources = midiBusSources;
                for (const auto& midiSource : inputMidiSources)
                    addRackConnection(rackType, midiSource, 0, node.id, 0, "chain midi bus");

                if (node.outputsPluginMidi) {
                    if (node.passesRawMidiInput) {
                        midiBusSources = inputMidiSources;
                        midiBusSources.push_back(node.id);
                    } else {
                        midiBusSources = {node.id};
                    }
                } else if (node.passesRawMidiInput) {
                    midiBusSources =
                        node.isInstrument ? inputMidiSources : std::vector<te::EditItemID>{node.id};
                }
            }

            if (node.isInstrument) {
                if (outputChannels >= 1) {
                    audioBusSources.push_back({node.id, 1, outputChannels >= 2 ? 2 : 1});
                }
                continue;
            }

            if (inputChannels > 0) {
                for (const auto& source : audioBusSources) {
                    addRackConnection(rackType, source.id, source.leftPin, node.id, 1,
                                      "chain audio bus left");
                    if (inputChannels >= 2) {
                        addRackConnection(rackType, source.id, source.rightPin, node.id, 2,
                                          "chain audio bus right");
                    }
                }
                audioBusSources.clear();
                if (outputChannels >= 1)
                    audioBusSources.push_back({node.id, 1, outputChannels >= 2 ? 2 : 1});
            }
        }

        if (chainActive) {
            for (const auto& midiSource : midiBusSources)
                addRackConnection(rackType, midiSource, 0, rackIOId, 0, "chain output midi");
            for (const auto& source : audioBusSources) {
                addRackConnection(rackType, source.id, source.leftPin, rackIOId, outLeftPin,
                                  "chain output left");
                addRackConnection(rackType, source.id, source.rightPin, rackIOId, outRightPin,
                                  "chain output right");
            }
            anyChainConnectedToOutput = true;
        }
    }

    // If no chains exist at all, pass audio and MIDI straight through so the
    // rack is transparent. When chains exist but are all muted/inactive, leave
    // audio disconnected for silence (MIDI is already wired per-chain above).
    if (!anyChainConnectedToOutput && rackInfo.chains.empty()) {
        rackType.addConnection(rackIOId, 1, rackIOId, 1);  // Audio L
        rackType.addConnection(rackIOId, 2, rackIOId, 2);  // Audio R
        rackType.addConnection(rackIOId, 0, rackIOId, 0);  // MIDI
    }
}

te::Plugin::Ptr RackSyncManager::createPluginForRack(TrackId trackId, const DeviceInfo& device) {
    return pluginManager_.createPluginOnly(trackId, device);
}

bool RackSyncManager::structureChanged(const SyncedRack& synced, const RackInfo& rackInfo) const {
    std::set<juce::String> expectedChains;
    std::set<DeviceId> expectedDevices;
    std::set<juce::String> expectedNestedRacks;

    std::function<void(const RackInfo&, const ChainNodePath&)> collect;
    collect = [&](const RackInfo& rack, const ChainNodePath& rackPath) {
        for (const auto& chain : rack.chains) {
            const auto chainPath = rackPath.withChain(chain.id);
            expectedChains.insert(pathKey(chainPath));
            for (const auto& element : chain.elements) {
                if (isDevice(element)) {
                    expectedDevices.insert(getDevice(element).id);
                } else if (isRack(element)) {
                    const auto& nestedRack = getRack(element);
                    const auto nestedPath = chainPath.withRack(nestedRack.id);
                    expectedNestedRacks.insert(pathKey(nestedPath));
                    collect(nestedRack, nestedPath);
                }
            }
        }
    };
    collect(rackInfo, ChainNodePath::rack(synced.trackId, rackInfo.id));

    if (synced.chainVolPanPlugins.size() != expectedChains.size())
        return true;
    for (const auto& chainKey : expectedChains)
        if (synced.chainVolPanPlugins.find(chainKey) == synced.chainVolPanPlugins.end())
            return true;

    if (synced.innerPlugins.size() != expectedDevices.size())
        return true;
    for (auto deviceId : expectedDevices)
        if (synced.innerPlugins.find(deviceId) == synced.innerPlugins.end())
            return true;

    if (synced.nestedRackInstances.size() != expectedNestedRacks.size())
        return true;
    for (const auto& key : expectedNestedRacks)
        if (synced.nestedRackInstances.find(key) == synced.nestedRackInstances.end())
            return true;

    return false;
}

void RackSyncManager::updateProperties(SyncedRack& synced, const RackInfo& rackInfo) {
    // Update rack bypass state
    applyBypassState(synced, rackInfo);

    const auto rackPath = ChainNodePath::rack(synced.trackId, rackInfo.id);
    updateElementPropertiesRecursive(synced, rackInfo, rackPath);

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
            rebuildConnectionsRecursive(synced, rackInfo, rackPath, *rackType);
        }
    }

    // Resync modifiers and macros (lightweight — just rebuilds TE modifier
    // assignments, no plugin state is lost)
    syncRackModulation(synced, rackInfo);
}

void RackSyncManager::updateElementPropertiesRecursive(SyncedRack& synced, const RackInfo& rackInfo,
                                                       const ChainNodePath& rackPath) {
    for (const auto& chain : rackInfo.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        auto volPanIt = synced.chainVolPanPlugins.find(pathKey(chainPath));
        if (volPanIt != synced.chainVolPanPlugins.end() && volPanIt->second) {
            if (auto* volPan = dynamic_cast<te::VolumeAndPanPlugin*>(volPanIt->second.get())) {
                volPan->setVolumeDb(chain.volume);
                volPan->setPan(chain.pan);
            }
        }

        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                auto pluginIt = synced.innerPlugins.find(device.id);
                if (pluginIt != synced.innerPlugins.end() && pluginIt->second)
                    pluginIt->second->setEnabled(!device.bypassed);
            } else if (isRack(element)) {
                const auto& nestedRack = getRack(element);
                const auto nestedPath = chainPath.withRack(nestedRack.id);
                auto instIt = synced.nestedRackInstances.find(pathKey(nestedPath));
                if (instIt != synced.nestedRackInstances.end())
                    applyRackInstanceState(instIt->second, nestedRack);
                updateElementPropertiesRecursive(synced, nestedRack, nestedPath);
            }
        }
    }
}

void RackSyncManager::rebuildConnectionsRecursive(SyncedRack& synced, const RackInfo& rackInfo,
                                                  const ChainNodePath& rackPath,
                                                  te::RackType& rackType) {
    for (const auto& chain : rackInfo.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        for (const auto& element : chain.elements) {
            if (!isRack(element))
                continue;
            const auto& nestedRack = getRack(element);
            const auto nestedPath = chainPath.withRack(nestedRack.id);
            auto typeIt = synced.nestedRackTypes.find(pathKey(nestedPath));
            if (typeIt == synced.nestedRackTypes.end() || !typeIt->second)
                continue;

            auto connections = typeIt->second->getConnections();
            for (int i = connections.size(); --i >= 0;) {
                auto* conn = connections[i];
                typeIt->second->removeConnection(conn->sourceID, conn->sourcePin, conn->destID,
                                                 conn->destPin);
            }
            rebuildConnectionsRecursive(synced, nestedRack, nestedPath, *typeIt->second);
        }
    }

    auto connections = rackType.getConnections();
    for (int i = connections.size(); --i >= 0;) {
        auto* conn = connections[i];
        rackType.removeConnection(conn->sourceID, conn->sourcePin, conn->destID, conn->destPin);
    }
    buildConnectionsForRack(synced, rackInfo, rackPath, rackType);
}

void RackSyncManager::applyBypassState(SyncedRack& synced, const RackInfo& rackInfo) {
    if (!synced.rackInstance)
        return;

    applyRackInstanceState(synced.rackInstance, rackInfo);
}

// =============================================================================
// Phase 2: Modifiers & Macros
// =============================================================================

// Adapter exposing SyncedRack::innerPlugins as a TargetPluginLookup. Used by
// syncRackModulation / updateRackModulationProperties; defined as a private
// nested type (rather than in an anonymous namespace) so it can refer to the
// class-private SyncedRack.
struct RackSyncManager::InnerPluginLookup : TargetPluginLookup {
    SyncedRack& synced;
    explicit InnerPluginLookup(SyncedRack& s) : synced(s) {}
    te::Plugin* getPlugin(const ChainNodePath& path) const override {
        auto it = synced.innerPlugins.find(path.getDeviceId());
        return (it != synced.innerPlugins.end() && it->second) ? it->second.get() : nullptr;
    }
};

void RackSyncManager::syncRackModulation(SyncedRack& synced, const RackInfo& rackInfo) {
    if (!synced.rackType)
        return;

    syncRackModulationRecursive(synced, rackInfo, ChainNodePath::rack(synced.trackId, rackInfo.id),
                                *synced.rackType);

    // Refresh the structural fingerprint so the next resyncAllModifiers
    // doesn't take the in-place property path against an out-of-date stored
    // fingerprint. Without this, a removeMod-triggered structural rebuild
    // (which goes through updateProperties → syncRackModulation) leaves the
    // fingerprint stale: a subsequent add+link with the same shape matches
    // the stale fingerprint, falls into syncProperties, finds an empty
    // state.modifiers map, and silently drops the new assignment — the
    // user sees the link wired in the UI but no audio modulation.
    rackFingerprints_[synced.rackId] = computeRackFingerprint(rackInfo);
}

void RackSyncManager::syncRackModulationRecursive(SyncedRack& synced, const RackInfo& rackInfo,
                                                  const ChainNodePath& rackPath,
                                                  te::RackType& rackType) {
    InnerPluginLookup lookup(synced);

    ModifierSyncContext ctx;
    ctx.modifierList = &rackType.getModifierList();
    ctx.macroList = &rackType.getMacroParameterListForWriting();
    ctx.lookup = &lookup;
    ctx.forEachScopePlugin = [&synced](const std::function<void(te::Plugin*)>& visit) {
        for (auto& [pluginId, plugin] : synced.innerPlugins) {
            if (plugin)
                visit(plugin.get());
        }
    };
    ctx.hasCrossTrackSidechain = false;

    const bool isTopLevelRack = rackPath.steps.size() == 1;
    auto& modifiers =
        isTopLevelRack ? synced.innerModifiers : synced.nestedRackMods[pathKey(rackPath)].modifiers;
    auto& curveSnapshots = isTopLevelRack ? synced.curveSnapshots
                                          : synced.nestedRackMods[pathKey(rackPath)].curveSnapshots;
    auto& macroParams = isTopLevelRack ? synced.innerMacroParams
                                       : synced.nestedRackMods[pathKey(rackPath)].macroParams;
    auto& innerDeviceMods = isTopLevelRack
                                ? synced.innerDeviceMods
                                : synced.nestedRackMods[pathKey(rackPath)].innerDeviceMods;

    ConstChainNode node;
    node.scope = ChainScope::Rack;
    node.trackId = synced.trackId;
    node.rackId = rackInfo.id;
    node.mods = &rackInfo.mods;
    node.macros = &rackInfo.macros;

    ModifierSyncState state{modifiers, curveSnapshots, macroParams};
    ModifierSyncWalker::syncStructure(node, ctx, state, deferredHolders_);

    // Sync per-rack-internal-device mods + macros. These are mods attached to
    // a device sitting inside a rack chain (DeviceInfo::mods on
    // rackInfo.chains[].elements[]). They live on the SAME rackType modifier
    // list as rack-scope mods, because the rack's audio graph is what
    // processes the inner plugins' parameters. Keyed per-DeviceId so the
    // ModId namespace doesn't collide with the rack-scope `innerModifiers`.
    auto innerDeviceIds = std::set<DeviceId>{};
    for (const auto& chain : rackInfo.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        for (const auto& chainElement : chain.elements) {
            if (isRack(chainElement)) {
                const auto& nestedRack = getRack(chainElement);
                const auto nestedRackPath = chainPath.withRack(nestedRack.id);
                auto typeIt = synced.nestedRackTypes.find(pathKey(nestedRackPath));
                if (typeIt != synced.nestedRackTypes.end() && typeIt->second)
                    syncRackModulationRecursive(synced, nestedRack, nestedRackPath,
                                                *typeIt->second);
                continue;
            }
            if (!isDevice(chainElement))
                continue;

            const auto& device = getDevice(chainElement);
            innerDeviceIds.insert(device.id);

            ConstChainNode deviceNode;
            deviceNode.scope = ChainScope::Device;
            deviceNode.deviceId = device.id;
            deviceNode.trackId = synced.trackId;
            deviceNode.mods = device.bypassed ? nullptr : &device.mods;
            deviceNode.macros = device.bypassed ? nullptr : &device.macros;

            ModifierSyncContext deviceCtx;
            deviceCtx.modifierList = ctx.modifierList;
            deviceCtx.macroList = ctx.macroList;
            deviceCtx.lookup = &lookup;
            deviceCtx.forEachScopePlugin = ctx.forEachScopePlugin;
            deviceCtx.hasCrossTrackSidechain = device.sidechain.sourceTrackId != INVALID_TRACK_ID;

            auto& devState = innerDeviceMods[device.id];
            ModifierSyncState devSyncState{devState.modifiers, devState.curveSnapshots,
                                           devState.macroParams};
            ModifierSyncWalker::syncStructure(deviceNode, deviceCtx, devSyncState,
                                              deferredHolders_);
        }
    }
    // Drop per-device state for devices that are no longer in the rack.
    for (auto it = innerDeviceMods.begin(); it != innerDeviceMods.end();) {
        if (innerDeviceIds.count(it->first) == 0)
            it = innerDeviceMods.erase(it);
        else
            ++it;
    }
}

void RackSyncManager::updateRackModulationProperties(SyncedRack& synced, const RackInfo& rackInfo) {
    updateRackModulationPropertiesRecursive(synced, rackInfo,
                                            ChainNodePath::rack(synced.trackId, rackInfo.id));
}

void RackSyncManager::updateRackModulationPropertiesRecursive(SyncedRack& synced,
                                                              const RackInfo& rackInfo,
                                                              const ChainNodePath& rackPath) {
    InnerPluginLookup lookup(synced);

    ModifierSyncContext ctx;
    ctx.lookup = &lookup;
    ctx.forEachScopePlugin = [&synced](const std::function<void(te::Plugin*)>& visit) {
        for (auto& [_pluginId, plugin] : synced.innerPlugins) {
            if (plugin)
                visit(plugin.get());
        }
    };

    const bool isTopLevelRack = rackPath.steps.size() == 1;
    auto& modifiers =
        isTopLevelRack ? synced.innerModifiers : synced.nestedRackMods[pathKey(rackPath)].modifiers;
    auto& curveSnapshots = isTopLevelRack ? synced.curveSnapshots
                                          : synced.nestedRackMods[pathKey(rackPath)].curveSnapshots;
    auto& macroParams = isTopLevelRack ? synced.innerMacroParams
                                       : synced.nestedRackMods[pathKey(rackPath)].macroParams;
    auto& innerDeviceMods = isTopLevelRack
                                ? synced.innerDeviceMods
                                : synced.nestedRackMods[pathKey(rackPath)].innerDeviceMods;

    ConstChainNode node;
    node.scope = ChainScope::Rack;
    node.trackId = synced.trackId;
    node.rackId = rackInfo.id;
    node.mods = &rackInfo.mods;
    node.macros = &rackInfo.macros;

    ModifierSyncState state{modifiers, curveSnapshots, macroParams};
    ModifierSyncWalker::syncProperties(node, ctx, state);

    // Note-trigger: the pre-step-2 updateAllModifierProperties also called
    // triggerLFONoteOnWithReset for any triggered LFO whose triggerMode wasn't
    // Free. Walker syncProperties leaves gate state alone (audio-thread owns
    // it), so we mirror the legacy retrigger here to preserve behaviour.
    auto retriggerIfNeeded = [](const ModInfo& modInfo,
                                const std::map<ModId, te::Modifier::Ptr>& mods) {
        if (!modInfo.enabled || !modInfo.triggered || modInfo.triggerMode == LFOTriggerMode::Free)
            return;
        auto it = mods.find(modInfo.id);
        if (it == mods.end() || !it->second)
            return;
        if (auto* lfo = dynamic_cast<te::LFOModifier*>(it->second.get()))
            triggerLFONoteOnWithReset(lfo);
    };
    for (const auto& modInfo : rackInfo.mods)
        retriggerIfNeeded(modInfo, modifiers);

    // Same property update + retrigger pass for each rack-internal device's
    // own mods. Keyed per-DeviceId so each device's ModId namespace is
    // resolved against the right state map.
    for (const auto& chain : rackInfo.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        for (const auto& chainElement : chain.elements) {
            if (isRack(chainElement)) {
                const auto& nestedRack = getRack(chainElement);
                updateRackModulationPropertiesRecursive(synced, nestedRack,
                                                        chainPath.withRack(nestedRack.id));
                continue;
            }
            if (!isDevice(chainElement))
                continue;
            const auto& device = getDevice(chainElement);
            auto devIt = innerDeviceMods.find(device.id);
            if (devIt == innerDeviceMods.end())
                continue;

            ConstChainNode deviceNode;
            deviceNode.scope = ChainScope::Device;
            deviceNode.deviceId = device.id;
            deviceNode.trackId = synced.trackId;
            deviceNode.mods = &device.mods;
            deviceNode.macros = &device.macros;

            ModifierSyncState devSyncState{devIt->second.modifiers, devIt->second.curveSnapshots,
                                           devIt->second.macroParams};
            ModifierSyncWalker::syncProperties(deviceNode, ctx, devSyncState);

            for (const auto& modInfo : device.mods)
                retriggerIfNeeded(modInfo, devIt->second.modifiers);
        }
    }
}

bool RackSyncManager::needsModifierResync(TrackId trackId) const {
    auto& tm = TrackManager::getInstance();
    auto* trackInfo = tm.getTrack(trackId);
    if (!trackInfo)
        return false;

    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (!isRack(element))
            continue;

        const auto& rack = getRack(element);
        auto it = syncedRacks_.find(rack.id);
        if (it == syncedRacks_.end())
            continue;

        auto storedIt = rackFingerprints_.find(rack.id);
        if (storedIt == rackFingerprints_.end() || computeRackFingerprint(rack) != storedIt->second)
            return true;
    }

    return false;
}

void RackSyncManager::collectLFOModifiers(TrackId trackId,
                                          std::vector<te::LFOModifier*>& out) const {
    auto collectFromMap = [&out](const std::map<ModId, te::Modifier::Ptr>& mods) -> int {
        int n = 0;
        for (const auto& [_modId, modifier] : mods) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get())) {
                out.push_back(lfo);
                ++n;
            }
        }
        return n;
    };
    for (const auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;
        collectFromMap(synced.innerModifiers);
        for (const auto& [_devId, devState] : synced.innerDeviceMods)
            collectFromMap(devState.modifiers);
        for (const auto& [_path, nestedState] : synced.nestedRackMods) {
            collectFromMap(nestedState.modifiers);
            for (const auto& [_devId, devState] : nestedState.innerDeviceMods)
                collectFromMap(devState.modifiers);
        }
    }
}

void RackSyncManager::collectLFOModifiersWithModes(TrackId trackId, std::vector<te::Modifier*>& out,
                                                   std::vector<LFOTriggerMode>& modes) const {
    auto& tm = TrackManager::getInstance();

    auto collectFromMods = [&](const std::vector<ModInfo>& magdaMods,
                               const std::map<ModId, te::Modifier::Ptr>& teMods) -> int {
        int n = 0;
        for (const auto& modInfo : magdaMods) {
            if (!modInfo.enabled || modInfo.links.empty())
                continue;
            auto it = teMods.find(modInfo.id);
            if (it == teMods.end() || !it->second)
                continue;
            if (dynamic_cast<te::LFOModifier*>(it->second.get()) ||
                dynamic_cast<te::ADSRModifier*>(it->second.get())) {
                out.push_back(it->second.get());
                modes.push_back(modInfo.triggerMode);
                ++n;
            }
        }
        return n;
    };

    for (const auto& entry : syncedRacks_) {
        const auto& rackId = entry.first;
        const auto& synced = entry.second;
        if (synced.trackId != trackId)
            continue;

        auto* rackInfo = tm.getRack(synced.trackId, rackId);
        if (!rackInfo)
            continue;

        auto collectRack =
            [&](auto&& self, RackInfo& rack, const ChainNodePath& rackPath,
                const std::map<ModId, te::Modifier::Ptr>& rackMods,
                const std::map<DeviceId, SyncedRack::InnerDeviceModState>& deviceMods) -> int {
            int collected = collectFromMods(rack.mods, rackMods);

            for (auto& chain : rack.chains) {
                const auto chainPath = rackPath.withChain(chain.id);
                for (auto& element : chain.elements) {
                    if (isDevice(element)) {
                        auto& device = getDevice(element);
                        auto devIt = deviceMods.find(device.id);
                        if (devIt != deviceMods.end())
                            collected += collectFromMods(device.mods, devIt->second.modifiers);
                    } else if (isRack(element)) {
                        auto& nestedRack = getRack(element);
                        const auto nestedPath = chainPath.withRack(nestedRack.id);
                        auto stateIt = synced.nestedRackMods.find(pathKey(nestedPath));
                        if (stateIt != synced.nestedRackMods.end()) {
                            collected +=
                                self(self, nestedRack, nestedPath, stateIt->second.modifiers,
                                     stateIt->second.innerDeviceMods);
                        }
                    }
                }
            }

            return collected;
        };

        collectRack(collectRack, *rackInfo, ChainNodePath::rack(synced.trackId, rackId),
                    synced.innerModifiers, synced.innerDeviceMods);
    }
}

void RackSyncManager::collectLFOModifiersWithModesForSidechainSource(
    TrackId destinationTrackId, TrackId sourceTrackId, std::vector<te::Modifier*>& out,
    std::vector<LFOTriggerMode>& modes) const {
    auto& tm = TrackManager::getInstance();

    auto collectFromMods = [&](const std::vector<ModInfo>& magdaMods,
                               const std::map<ModId, te::Modifier::Ptr>& teMods) -> int {
        int n = 0;
        for (const auto& modInfo : magdaMods) {
            if (!modInfo.enabled || modInfo.links.empty())
                continue;
            auto it = teMods.find(modInfo.id);
            if (it == teMods.end() || !it->second)
                continue;
            if (dynamic_cast<te::LFOModifier*>(it->second.get()) ||
                dynamic_cast<te::ADSRModifier*>(it->second.get())) {
                out.push_back(it->second.get());
                modes.push_back(modInfo.triggerMode);
                ++n;
            }
        }
        return n;
    };

    auto rackContainsSidechainSource = [&](auto&& self, const RackInfo& rack) -> bool {
        if (rack.sidechain.sourceTrackId == sourceTrackId)
            return true;

        for (const auto& chain : rack.chains) {
            for (const auto& element : chain.elements) {
                if (isDevice(element)) {
                    if (getDevice(element).sidechain.sourceTrackId == sourceTrackId)
                        return true;
                } else if (isRack(element)) {
                    if (self(self, getRack(element)))
                        return true;
                }
            }
        }
        return false;
    };

    for (const auto& entry : syncedRacks_) {
        const auto& rackId = entry.first;
        const auto& synced = entry.second;
        if (synced.trackId != destinationTrackId)
            continue;

        auto* rackInfo = tm.getRack(synced.trackId, rackId);
        if (!rackInfo)
            continue;

        auto collectRack =
            [&](auto&& self, RackInfo& rack, const ChainNodePath& rackPath,
                const std::map<ModId, te::Modifier::Ptr>& rackMods,
                const std::map<DeviceId, SyncedRack::InnerDeviceModState>& deviceMods) -> int {
            int collected = 0;
            const auto rackSource = rack.sidechain.sourceTrackId;
            const bool rackSourceMatches =
                rackSource == sourceTrackId ||
                (rackSource == INVALID_TRACK_ID &&
                 rackContainsSidechainSource(rackContainsSidechainSource, rack));

            if (rackSourceMatches)
                collected += collectFromMods(rack.mods, rackMods);

            for (auto& chain : rack.chains) {
                const auto chainPath = rackPath.withChain(chain.id);
                for (auto& element : chain.elements) {
                    if (isDevice(element)) {
                        auto& device = getDevice(element);
                        if (device.sidechain.sourceTrackId != sourceTrackId)
                            continue;
                        auto devIt = deviceMods.find(device.id);
                        if (devIt != deviceMods.end())
                            collected += collectFromMods(device.mods, devIt->second.modifiers);
                    } else if (isRack(element)) {
                        auto& nestedRack = getRack(element);
                        const auto nestedPath = chainPath.withRack(nestedRack.id);
                        auto stateIt = synced.nestedRackMods.find(pathKey(nestedPath));
                        if (stateIt != synced.nestedRackMods.end()) {
                            collected +=
                                self(self, nestedRack, nestedPath, stateIt->second.modifiers,
                                     stateIt->second.innerDeviceMods);
                        }
                    }
                }
            }

            return collected;
        };

        collectRack(collectRack, *rackInfo, ChainNodePath::rack(synced.trackId, rackId),
                    synced.innerModifiers, synced.innerDeviceMods);
    }
}

void RackSyncManager::syncLFOValuesToVisuals() {
    auto& tm = TrackManager::getInstance();
    auto overlayInto = [](std::vector<ModInfo>& magdaMods,
                          const std::map<ModId, te::Modifier::Ptr>& teMods) {
        if (teMods.empty())
            return;
        for (auto& magdaMod : magdaMods) {
            auto it = teMods.find(magdaMod.id);
            if (it == teMods.end() || !it->second)
                continue;
            // Only overlay when the local sim considers this mod "running".
            // For triggered modes (MIDI/Audio), the audio LFO keeps free-
            // running between notes (TE syncType=note resets phase on
            // note-on, doesn't gate on note-off), but the local sim
            // correctly clamps value=0 after note release. Without this
            // gate the visual would keep animating forever once triggered.
            // Envelopes overlay unconditionally: the release stage plays out
            // after the gate closes and idle reads 0, so the value is always
            // meaningful (unlike a free-running LFO that must freeze on note
            // release in the visual).
            if (magdaMod.type == ModType::Envelope) {
                overlayModifierVisuals(magdaMod, it->second.get());
                continue;
            }
            const bool running = (magdaMod.triggerMode == LFOTriggerMode::Free) || magdaMod.running;
            if (!running)
                continue;
            overlayModifierVisuals(magdaMod, it->second.get());
        }
    };
    for (auto& entry : syncedRacks_) {
        const auto& rackId = entry.first;
        auto& synced = entry.second;
        auto* rackInfo = tm.getRack(synced.trackId, rackId);
        if (!rackInfo)
            continue;

        auto overlayRack =
            [&](auto&& self, RackInfo& rack, const ChainNodePath& rackPath,
                const std::map<ModId, te::Modifier::Ptr>& rackMods,
                const std::map<DeviceId, SyncedRack::InnerDeviceModState>& deviceMods) -> void {
            overlayInto(rack.mods, rackMods);

            for (auto& chain : rack.chains) {
                const auto chainPath = rackPath.withChain(chain.id);
                for (auto& element : chain.elements) {
                    if (isDevice(element)) {
                        auto& device = getDevice(element);
                        auto devIt = deviceMods.find(device.id);
                        if (devIt != deviceMods.end())
                            overlayInto(device.mods, devIt->second.modifiers);
                    } else if (isRack(element)) {
                        auto& nestedRack = getRack(element);
                        const auto nestedPath = chainPath.withRack(nestedRack.id);
                        auto nestedIt = synced.nestedRackMods.find(pathKey(nestedPath));
                        if (nestedIt != synced.nestedRackMods.end()) {
                            self(self, nestedRack, nestedPath, nestedIt->second.modifiers,
                                 nestedIt->second.innerDeviceMods);
                        }
                    }
                }
            }
        };

        overlayRack(overlayRack, *rackInfo, ChainNodePath::rack(synced.trackId, rackId),
                    synced.innerModifiers, synced.innerDeviceMods);
    }
}

void RackSyncManager::ungateAllLFOs() {
    auto ungate = [](const std::map<ModId, te::Modifier::Ptr>& mods) {
        for (const auto& [_modId, modifier] : mods)
            setModifierGated(modifier.get(), false);
    };
    for (auto& [rackId, synced] : syncedRacks_) {
        ungate(synced.innerModifiers);
        for (auto& [_devId, devState] : synced.innerDeviceMods)
            ungate(devState.modifiers);
        for (auto& [_path, nestedState] : synced.nestedRackMods) {
            ungate(nestedState.modifiers);
            for (auto& [_devId, devState] : nestedState.innerDeviceMods)
                ungate(devState.modifiers);
        }
    }
}

void RackSyncManager::regateTriggeredLFOs() {
    auto regate = [](const std::map<ModId, te::Modifier::Ptr>& mods) {
        for (const auto& [_modId, modifier] : mods)
            if (modifierSkipsNativeResync(modifier.get()))
                setModifierGated(modifier.get(), true);
    };
    for (auto& [rackId, synced] : syncedRacks_) {
        regate(synced.innerModifiers);
        for (auto& [_devId, devState] : synced.innerDeviceMods)
            regate(devState.modifiers);
        for (auto& [_path, nestedState] : synced.nestedRackMods) {
            regate(nestedState.modifiers);
            for (auto& [_devId, devState] : nestedState.innerDeviceMods)
                regate(devState.modifiers);
        }
    }
}

void RackSyncManager::triggerLFONoteOn(TrackId trackId) {
    auto trigger = [](const std::map<ModId, te::Modifier::Ptr>& mods) {
        for (const auto& [_modId, modifier] : mods)
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(modifier.get()))
                triggerLFONoteOnWithReset(lfo);
    };
    for (auto& [rackId, synced] : syncedRacks_) {
        if (synced.trackId != trackId)
            continue;
        trigger(synced.innerModifiers);
        for (auto& [_devId, devState] : synced.innerDeviceMods)
            trigger(devState.modifiers);
        for (auto& [_path, nestedState] : synced.nestedRackMods) {
            trigger(nestedState.modifiers);
            for (auto& [_devId, devState] : nestedState.innerDeviceMods)
                trigger(devState.modifiers);
        }
    }
}

}  // namespace magda
