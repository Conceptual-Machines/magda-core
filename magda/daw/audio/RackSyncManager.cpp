#include "RackSyncManager.hpp"

#include <iostream>

#include "PluginManager.hpp"

namespace magda {

RackSyncManager::RackSyncManager(te::Edit& edit, PluginManager& pluginManager)
    : edit_(edit), pluginManager_(pluginManager) {}

// =============================================================================
// Public API
// =============================================================================

te::Plugin::Ptr RackSyncManager::syncRack(TrackId trackId, const RackInfo& rackInfo) {
    // Check if already synced
    auto it = syncedRacks_.find(rackInfo.id);
    if (it != syncedRacks_.end()) {
        if (structureChanged(it->second, rackInfo)) {
            resyncRack(trackId, rackInfo);
        } else {
            updateProperties(it->second, rackInfo);
        }
        return it->second.rackInstance;
    }

    // 1. Create a new RackType in the edit
    auto rackType = edit_.getRackList().addNewRack();
    if (!rackType) {
        std::cerr << "RackSyncManager: Failed to create RackType for rack " << rackInfo.id
                  << std::endl;
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
        std::cerr << "RackSyncManager: Failed to create RackInstance for rack " << rackInfo.id
                  << std::endl;
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

void RackSyncManager::removeRack(RackId rackId) {
    auto it = syncedRacks_.find(rackId);
    if (it == syncedRacks_.end())
        return;

    auto& synced = it->second;

    // Remove the RackInstance from its parent track
    if (synced.rackInstance) {
        synced.rackInstance->deleteFromParent();
    }

    // Remove the RackType from the edit
    if (synced.rackType) {
        edit_.getRackList().removeRackType(synced.rackType);
    }

    DBG("RackSyncManager: Removed rack " << rackId);

    syncedRacks_.erase(it);
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

void RackSyncManager::clear() {
    syncedRacks_.clear();
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

                        // Apply bypass state
                        plugin->setEnabled(!device.bypassed);

                        DBG("RackSyncManager: Added plugin '" << device.name << "' (device "
                                                              << device.id << ") to rack "
                                                              << synced.rackId);
                    } else {
                        std::cerr << "RackSyncManager: Failed to add plugin '" << device.name
                                  << "' to rack" << std::endl;
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

        // Connect last plugin to rack output (only if chain is active)
        if (chainActive) {
            rackType->addConnection(lastPlugin, 1, rackIOId, 1);  // Left
            rackType->addConnection(lastPlugin, 2, rackIOId, 2);  // Right
            anyChainConnectedToOutput = true;
        }
    }

    // If no chain connected to output (all empty, muted, or no chains),
    // pass audio straight through so the rack is transparent
    if (!anyChainConnectedToOutput) {
        rackType->addConnection(rackIOId, 1, rackIOId, 1);
        rackType->addConnection(rackIOId, 2, rackIOId, 2);
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
    if (rackInfo.volume != 0.0f) {
        rackInstance->leftOutDb->setParameter(
            static_cast<float>(juce::jlimit(te::RackInstance::rackMinDb,
                                            te::RackInstance::rackMaxDb,
                                            static_cast<double>(rackInfo.volume))),
            juce::dontSendNotification);
        rackInstance->rightOutDb->setParameter(
            static_cast<float>(juce::jlimit(te::RackInstance::rackMinDb,
                                            te::RackInstance::rackMaxDb,
                                            static_cast<double>(rackInfo.volume))),
            juce::dontSendNotification);
    }
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
    for (auto& [modId, mod] : synced.innerModifiers) {
        if (mod) {
            modList.state.removeChild(mod->state, nullptr);
        }
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
                    // Map waveform
                    float waveVal = 0.0f;  // sine
                    switch (modInfo.waveform) {
                        case LFOWaveform::Sine:
                            waveVal = 0.0f;
                            break;
                        case LFOWaveform::Triangle:
                            waveVal = 1.0f;
                            break;
                        case LFOWaveform::Saw:
                            waveVal = 2.0f;
                            break;
                        case LFOWaveform::ReverseSaw:
                            waveVal = 3.0f;
                            break;
                        case LFOWaveform::Square:
                            waveVal = 4.0f;
                            break;
                        case LFOWaveform::Custom:
                            waveVal = 0.0f;
                            break;  // Fallback to sine
                    }
                    lfo->wave = waveVal;
                    lfo->rate = modInfo.rate;
                    lfo->depth = 1.0f;  // Depth controlled per-assignment via link.amount
                    lfo->phase = modInfo.phaseOffset;

                    // Sync type: 0 = free, 1 = transport
                    lfo->syncType = modInfo.tempoSync ? 1.0f : 0.0f;

                    if (modInfo.tempoSync) {
                        // Map SyncDivision to TE RateType
                        float rateType = 2.0f;  // quarter note default
                        switch (modInfo.syncDivision) {
                            case SyncDivision::Whole:
                                rateType = 0.0f;
                                break;
                            case SyncDivision::Half:
                                rateType = 1.0f;
                                break;
                            case SyncDivision::Quarter:
                                rateType = 2.0f;
                                break;
                            case SyncDivision::Eighth:
                                rateType = 3.0f;
                                break;
                            case SyncDivision::Sixteenth:
                                rateType = 4.0f;
                                break;
                            case SyncDivision::ThirtySecond:
                                rateType = 5.0f;
                                break;
                            case SyncDivision::DottedHalf:
                                rateType = 1.0f;
                                break;  // Closest
                            case SyncDivision::DottedQuarter:
                                rateType = 2.0f;
                                break;
                            case SyncDivision::DottedEighth:
                                rateType = 3.0f;
                                break;
                            case SyncDivision::TripletHalf:
                                rateType = 1.0f;
                                break;
                            case SyncDivision::TripletQuarter:
                                rateType = 2.0f;
                                break;
                            case SyncDivision::TripletEighth:
                                rateType = 3.0f;
                                break;
                        }
                        lfo->rateType = rateType;
                    }
                }
                modifier = lfoMod;
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
                    param->addModifier(*modifier, link.amount);
                }
            }
        }
    }
}

void RackSyncManager::syncMacros(SyncedRack& synced, const RackInfo& rackInfo) {
    synced.innerMacroParams.clear();

    auto& rackType = synced.rackType;
    if (!rackType)
        return;

    auto& macroList = rackType->getMacroParameterListForWriting();

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

}  // namespace magda
