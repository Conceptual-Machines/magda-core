#include <map>
#include <set>
#include <unordered_set>
#include <vector>

#include "../../core/InternalDeviceKind.hpp"
#include "../../core/PluginCapabilities.hpp"
#include "../../core/RackInfo.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/aliases/AutoAliasGenerator.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../PluginWindowBridge.hpp"
#include "../TrackController.hpp"
#include "../TracktionHelpers.hpp"
#include "ExternalPluginStateUtil.hpp"
#include "PluginManager.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "modifiers/ModifierHelpers.hpp"
#include "modifiers/ModifierSync.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/AudioSidechainMonitorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiDevicePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "processors/DeviceProcessor.hpp"
#include "processors/DeviceProcessorFactory.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

namespace {
void clearAutomationCurve(te::AutomatableParameter* param) {
    if (!param)
        return;

    param->getCurve().clear(nullptr);
    param->updateStream();
}

void removeSourceFromPlugin(te::Plugin* plugin, te::AutomatableParameter::ModifierSource& source) {
    if (!plugin)
        return;

    for (auto* param : plugin->getAutomatableParameters()) {
        if (param)
            param->removeModifier(source);
    }
}

void removeSourceFromPlugins(const std::vector<te::Plugin*>& plugins,
                             te::AutomatableParameter::ModifierSource& source) {
    for (auto* plugin : plugins)
        removeSourceFromPlugin(plugin, source);
}

const char* pluginFormatText(PluginFormat format) {
    switch (format) {
        case PluginFormat::VST3:
            return "VST3";
        case PluginFormat::AU:
            return "AU";
        case PluginFormat::VST:
            return "VST";
        case PluginFormat::Internal:
            return "Internal";
    }

    return "Unknown";
}

bool pluginProducesMidi(te::Plugin& plugin) {
    if (auto* processor = plugin.getWrappedAudioProcessor())
        return processor->producesMidi() || processor->isMidiEffect();

    return dynamic_cast<daw::audio::MidiDevicePlugin*>(&plugin) != nullptr;
}

PluginCapabilitySnapshot makePluginCapabilitySnapshot(const DeviceInfo& device,
                                                      te::Plugin& plugin) {
    PluginCapabilitySnapshot snapshot;
    snapshot.pluginIdentifier = PluginCapabilityCache::identifierForDevice(device);
    snapshot.name = device.name.isNotEmpty() ? device.name : plugin.getName();
    snapshot.manufacturer =
        device.manufacturer.isNotEmpty() ? device.manufacturer : plugin.getVendor();
    snapshot.format = pluginFormatText(device.format);
    snapshot.tracktionTakesMidiInput = plugin.takesMidiInput();
    snapshot.tracktionTakesAudioInput = plugin.takesAudioInput();
    snapshot.tracktionProducesAudioWhenNoAudioInput = plugin.producesAudioWhenNoAudioInput();
    snapshot.hasMidiOutput = pluginProducesMidi(plugin);
    snapshot.hasMidiInput = snapshot.tracktionTakesMidiInput;
    snapshot.hasAudioInput = snapshot.tracktionTakesAudioInput;
    snapshot.hasAudioOutput = plugin.getNumOutputChannelsGivenInputs(0) > 0 ||
                              plugin.getNumOutputChannelsGivenInputs(2) > 0;

    if (auto* processor = plugin.getWrappedAudioProcessor()) {
        snapshot.processorAcceptsMidi = processor->acceptsMidi();
        snapshot.processorProducesMidi = processor->producesMidi();
        snapshot.processorIsMidiEffect = processor->isMidiEffect();
        snapshot.audioInputChannels = processor->getTotalNumInputChannels();
        snapshot.audioOutputChannels = processor->getTotalNumOutputChannels();
        snapshot.inputBusCount = processor->getBusCount(true);
        snapshot.outputBusCount = processor->getBusCount(false);
        snapshot.hasMidiInput = snapshot.hasMidiInput || snapshot.processorAcceptsMidi;
        snapshot.hasMidiOutput = snapshot.hasMidiOutput || snapshot.processorProducesMidi ||
                                 snapshot.processorIsMidiEffect;
        snapshot.hasAudioInput = snapshot.hasAudioInput || snapshot.audioInputChannels > 0;
        snapshot.hasAudioOutput = snapshot.hasAudioOutput || snapshot.audioOutputChannels > 0;
    }

    return snapshot;
}

void updateDeviceCapabilityFlags(DeviceInfo& device, te::Plugin& plugin) {
    const auto snapshot = makePluginCapabilitySnapshot(device, plugin);
    PluginCapabilityCache::getInstance().update(snapshot);

    if (plugin.canSidechain())
        device.canSidechain = true;
    if (snapshot.hasMidiInput && !device.isInstrument)
        device.canReceiveMidi = true;
    device.producesMidi = snapshot.hasMidiOutput;
}

void removeSourceFromModifierParams(const std::map<ModId, te::Modifier::Ptr>& modifiers,
                                    te::AutomatableParameter::ModifierSource& source) {
    for (const auto& [_modId, modifier] : modifiers) {
        if (!modifier)
            continue;

        for (auto* param : modifier->getAutomatableParameters()) {
            if (param)
                param->removeModifier(source);
        }
    }
}

void clearModifierParameterCurves(te::Modifier& modifier) {
    for (auto* param : modifier.getAutomatableParameters())
        clearAutomationCurve(param);
}

void teardownModifierMap(std::map<ModId, te::Modifier::Ptr>& modifiers,
                         const std::vector<te::Plugin*>& scopePlugins,
                         te::ModifierList* modifierList) {
    for (auto& [_modId, modifier] : modifiers) {
        if (!modifier)
            continue;

        clearModifierParameterCurves(*modifier);
        removeSourceFromPlugins(scopePlugins, *modifier);
        removeSourceFromModifierParams(modifiers, *modifier);

        if (modifierList && modifier->state.getParent().isValid())
            modifierList->state.removeChild(modifier->state, nullptr);
    }
    modifiers.clear();
}

void teardownMacroMap(std::map<int, te::MacroParameter*>& macros,
                      const std::map<ModId, te::Modifier::Ptr>& modifiers,
                      const std::vector<te::Plugin*>& scopePlugins,
                      te::MacroParameterList* macroList) {
    for (auto& [_macroIdx, macroParam] : macros) {
        if (!macroParam)
            continue;

        clearAutomationCurve(macroParam);
        removeSourceFromPlugins(scopePlugins, *macroParam);
        removeSourceFromModifierParams(modifiers, *macroParam);

        if (macroList && macroParam->state.getParent() == macroList->state)
            macroList->removeMacroParameter(*macroParam);
    }
    macros.clear();
}

void collectChainDevicePaths(TrackId trackId, const std::vector<ChainElement>& elements,
                             const ChainNodePath& parentPath,
                             std::vector<ChainNodePath>& devicePaths,
                             std::set<RackId>* rackIds = nullptr) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            if (parentPath.trackId == INVALID_TRACK_ID) {
                devicePaths.push_back(ChainNodePath::topLevelDevice(trackId, device.id));
            } else {
                devicePaths.push_back(parentPath.withDevice(device.id));
            }
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            if (rackIds)
                rackIds->insert(rack.id);

            ChainNodePath rackPath;
            if (parentPath.trackId == INVALID_TRACK_ID)
                rackPath = ChainNodePath::rack(trackId, rack.id);
            else
                rackPath = parentPath.withRack(rack.id);

            for (const auto& chain : rack.chains)
                collectChainDevicePaths(trackId, chain.elements, rackPath.withChain(chain.id),
                                        devicePaths, rackIds);
        }
    }
}

DeviceInfo* getDeviceInfoForPath(const ChainNodePath& devicePath) {
    auto& tm = TrackManager::getInstance();
    if (auto* device = tm.getDeviceInChainByPath(devicePath))
        return device;
    return tm.getDevice(devicePath.trackId, devicePath.getDeviceId());
}

bool savedPluginStateMatchesRequestedType(const juce::ValueTree& savedState,
                                          const juce::String& requestedType) {
    const auto savedType = savedState.getProperty(te::IDs::type).toString();
    if (savedType.isEmpty())
        return false;
    if (savedType.equalsIgnoreCase(requestedType))
        return true;

    if (auto* requestedCompiled = daw::audio::compiled::findCompiledPluginSpec(requestedType)) {
        auto* savedCompiled = daw::audio::compiled::findCompiledPluginSpec(savedType);
        return savedCompiled == requestedCompiled;
    }

    if (auto* requestedInternal = daw::audio::findInternalPluginSpecForLoadType(requestedType)) {
        auto* savedInternal = daw::audio::findInternalPluginSpecForLoadType(savedType);
        return savedInternal == requestedInternal;
    }

    return false;
}

// Restore a loaded device's state as ONE ordered operation, so the
// baseline -> overlay -> cache-refresh sequence lives in a single place and
// can't be reordered (or half-applied) by callers:
//   1. syncFromDeviceInfo applies the saved per-parameter array (plus gain/bypass)
//      as a BASELINE -- the fallback that survives a missing/rejected/incomplete
//      native chunk, and the sole source of truth for parameter-only devices.
//   2. applyExternalPluginChunk applies the native state chunk as the AUTHORITATIVE
//      overlay (wins wherever it restores) and refreshes TE's parameter cache so
//      the playback-graph build preserves the merged result rather than writing
//      construction-time defaults back.
// Safe for any device type: the overlay no-ops for internal plugins / empty chunk,
// leaving just the baseline.
void restoreDeviceStateWithChunkOverlay(DeviceProcessor& processor, const te::Plugin::Ptr& plugin,
                                        const DeviceInfo& device) {
    processor.syncFromDeviceInfo(device);
    // DAWproject-imported VST3s carry their state as a .vstpreset (vst3Preset)
    // rather than MAGDA's TE chunk; apply it as the authoritative overlay too.
    if (device.vst3Preset.isNotEmpty())
        applyVst3Preset(plugin.get(), device.vst3Preset);
    else
        applyExternalPluginChunk(plugin.get(), device.pluginState);
}

bool canOwnInstrumentWrapper(const ChainNodePath& devicePath) {
    return devicePath.getType() == ChainNodeType::TopLevelDevice;
}
}  // namespace

// =============================================================================
// Plugin Synchronization
// =============================================================================

void PluginManager::syncAllPlugins() {
    auto& tm = TrackManager::getInstance();
    const auto& tracks = tm.getTracks();

    // ── Step 1: Collect all valid device/rack IDs across ALL tracks ──────
    std::set<ChainNodePath> validDevicePaths;
    std::set<RackId> validRackIds;

    auto collectTrackPaths = [&](const TrackInfo& track) {
        std::vector<ChainNodePath> paths;
        collectChainDevicePaths(track.id, track.chain.fxChainElements, {}, paths, &validRackIds);
        for (const auto& path : paths)
            validDevicePaths.insert(path);
        for (const auto& elem : track.chain.postFxChainElements)
            validDevicePaths.insert(ChainNodePath::postFxDevice(track.id, elem.device.id));
        for (const auto& elem : track.chain.mixerAnalysisElements)
            validDevicePaths.insert(ChainNodePath::mixerAnalysisDevice(track.id, elem.device.id));
    };

    for (const auto& track : tracks) {
        collectTrackPaths(track);
    }

    // Include master track (not in getTracks())
    if (auto* masterTrack = tm.getTrack(MASTER_TRACK_ID)) {
        collectTrackPaths(*masterTrack);
    }

    // ── Step 2: Remove orphan devices (globally) ────────────────────────
    {
        std::vector<ChainNodePath> orphanDevicePaths;
        std::vector<te::Plugin::Ptr> pluginsToDelete;
        std::vector<te::Plugin*> midiPluginsToDelete;
        std::vector<te::Plugin*> monitorPluginsToDelete;
        {
            juce::ScopedLock lock(pluginLock_);
            deferredHolders_.clear();  // Drain previous cycle's deferred holders
            for (auto it = syncedDevices_.begin(); it != syncedDevices_.end();) {
                if (validDevicePaths.find(it->first) == validDevicePaths.end()) {
                    std::vector<te::Plugin*> scopePlugins;
                    for (const auto& [_deviceId, sd] : syncedDevices_) {
                        if (sd.trackId == it->second.trackId && sd.plugin)
                            scopePlugins.push_back(sd.plugin.get());
                    }
                    auto* teTrack = trackController_.getAudioTrack(it->second.trackId);
                    auto* modifierList = teTrack ? teTrack->getModifierList() : nullptr;
                    auto* macroList =
                        teTrack ? &teTrack->getMacroParameterListForWriting() : nullptr;

                    clearLFOCustomWaveCallbacks(it->second.modifiers);
                    teardownMacroMap(it->second.macroParams, it->second.modifiers, scopePlugins,
                                     macroList);
                    teardownModifierMap(it->second.modifiers, scopePlugins, modifierList);
                    deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                    if (auto* dg =
                            dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get()))
                        dg->removeListener(this);
                    if (it->second.plugin)
                        pluginToDevice_.erase(it->second.plugin.get());
                    if (it->second.midiReceivePlugin)
                        midiPluginsToDelete.push_back(it->second.midiReceivePlugin.get());
                    if (it->second.midiRestorePlugin)
                        midiPluginsToDelete.push_back(it->second.midiRestorePlugin.get());
                    orphanDevicePaths.push_back(it->first);
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
                        monitorPluginsToDelete.push_back(it->second.get());
                    it = sidechainMonitors_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Close windows and delete plugins outside lock
        for (const auto& devicePath : orphanDevicePaths) {
            const auto deviceId = devicePath.getDeviceId();
            pluginWindowBridge_.closeWindowsForDevice(deviceId);
            if (canOwnInstrumentWrapper(devicePath) &&
                instrumentRackManager_.getInnerPlugin(deviceId) != nullptr)
                instrumentRackManager_.unwrap(deviceId);
        }
        for (auto* plugin : midiPluginsToDelete)
            if (plugin)
                plugin->deleteFromParent();
        for (auto& plugin : pluginsToDelete)
            plugin->deleteFromParent();
        for (auto* plugin : monitorPluginsToDelete)
            if (plugin)
                plugin->deleteFromParent();
    }

    // ── Step 3: Remove orphan racks (globally) ──────────────────────────
    {
        auto syncedRackIds = rackSyncManager_.getSyncedRackIds();
        for (auto rackId : syncedRackIds) {
            if (validRackIds.find(rackId) == validRackIds.end()) {
                rackSyncManager_.removeRack(rackId);
            }
        }
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
    std::vector<ChainNodePath> magdaDevices;
    std::vector<RackId> magdaRacks;
    std::set<RackId> magdaRackSet;
    collectChainDevicePaths(trackId, trackInfo->chain.fxChainElements, {}, magdaDevices,
                            &magdaRackSet);
    magdaRacks.assign(magdaRackSet.begin(), magdaRackSet.end());

    // Post-FX devices are flat (no racks/instruments) and run before the fader.
    // Include them so stale-removal keeps their plugins (and removes deleted ones).
    for (const auto& postElem : trackInfo->chain.postFxChainElements)
        magdaDevices.push_back(ChainNodePath::postFxDevice(trackId, postElem.device.id));
    // Mixer-analysis devices: same shape as post-FX, rail-managed.
    for (const auto& miniElem : trackInfo->chain.mixerAnalysisElements)
        magdaDevices.push_back(ChainNodePath::mixerAnalysisDevice(trackId, miniElem.device.id));

    // Remove TE plugins that no longer exist in MAGDA for THIS track.
    // Uses the stored trackId for ownership — no TE owner-track heuristic needed.
    std::vector<ChainNodePath> toRemove;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [devicePath, sd] : syncedDevices_) {
            if (!sd.plugin || sd.trackId != trackId)
                continue;

            bool found = std::find(magdaDevices.begin(), magdaDevices.end(), devicePath) !=
                         magdaDevices.end();
            if (!found) {
                toRemove.push_back(devicePath);
                pluginsToDelete.push_back(sd.plugin);
            }
        }

        // Remove from mappings while under lock
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        std::vector<te::Plugin*> scopePlugins;
        for (const auto& [_deviceId, sd] : syncedDevices_) {
            if (sd.trackId == trackId && sd.plugin)
                scopePlugins.push_back(sd.plugin.get());
        }
        auto* modifierList = teTrack ? teTrack->getModifierList() : nullptr;
        auto* macroList = teTrack ? &teTrack->getMacroParameterListForWriting() : nullptr;
        for (const auto& devicePath : toRemove) {
            auto it = findSyncedDevice(devicePath);
            if (it != syncedDevices_.end()) {
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                teardownMacroMap(it->second.macroParams, it->second.modifiers, scopePlugins,
                                 macroList);
                teardownModifierMap(it->second.modifiers, scopePlugins, modifierList);

                // Clear LFO callbacks before destroying CurveSnapshotHolders
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
                    dg->removeListener(this);
                    // Remove pad plugin entries for this DrumGrid
                    removeDrumGridPadDevicesLocked(devicePath);
                }
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                syncedDevices_.erase(it);
            }
        }
    }

    // Delete plugins outside lock to avoid blocking other threads
    for (size_t i = 0; i < toRemove.size(); ++i) {
        const auto devicePath = toRemove[i];
        const auto deviceId = devicePath.getDeviceId();
        pluginWindowBridge_.closeWindowsForDevice(deviceId);

        // Remove any orphaned MidiReceivePlugin for this device
        removeMidiReceive(devicePath);

        // If this was a wrapped instrument, unwrap it (removes rack + rack type)
        if (canOwnInstrumentWrapper(devicePath) &&
            instrumentRackManager_.getInnerPlugin(deviceId) != nullptr) {
            instrumentRackManager_.unwrap(deviceId);
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
    for (size_t elemIdx = 0; elemIdx < trackInfo->chain.fxChainElements.size(); ++elemIdx) {
        const auto& element = trackInfo->chain.fxChainElements[elemIdx];
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            const auto devicePath = ChainNodePath::topLevelDevice(trackId, device.id);

            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(devicePath) == syncedDevices_.end()) {
                // Compute TE insertion index: find the first subsequent chain element
                // that already has a synced plugin, and insert before it.
                int teInsertIndex = -1;  // -1 = append (before VolumeAndPan/LevelMeter)
                auto* teTrackForIdx = trackController_.getAudioTrack(trackId);
                for (size_t j = elemIdx + 1;
                     teTrackForIdx && j < trackInfo->chain.fxChainElements.size(); ++j) {
                    if (isDevice(trackInfo->chain.fxChainElements[j])) {
                        auto nextId = getDevice(trackInfo->chain.fxChainElements[j]).id;
                        auto it = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, nextId));
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

                auto plugin = loadDeviceAsPlugin(devicePath, device, teInsertIndex);
                if (plugin) {
                    auto& sd = syncedDevices_[devicePath];
                    sd.trackId = trackId;
                    sd.plugin = plugin;
                    pluginToDevice_[plugin.get()] = devicePath;

                    // Check if plugin is still loading asynchronously (external plugins)
                    if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                        if (extPlugin->isInitialisingAsync()) {
                            sd.isPendingLoad = true;

                            if (auto* devInfo =
                                    TrackManager::getInstance().getDevice(trackId, device.id)) {
                                devInfo->loadState = DeviceLoadState::Loading;
                            }

                            // Notify so UI rebuilds with the Loading indicator
                            TrackManager::getInstance().notifyTrackDevicesChanged(trackId);

                            // Poll for completion — TE's async callback runs on message
                            // thread, so a short timer will catch it promptly
                            pollAsyncPluginLoad(devicePath, plugin);
                        }
                    }
                }
            }
        } else if (isRack(element)) {
            const auto& rackInfo = getRack(element);

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
                                innerPlugin->flushPluginStateToValueTree();
                                auto stateCopy = innerPlugin->state.createCopy();
                                stateCopy.removeProperty(te::IDs::id, nullptr);
                                if (auto xml = stateCopy.createXml())
                                    devInfo.pluginState = xml->toString();
                            }

                            instrumentRackManager_.unwrap(devId);

                            // Also remove from syncedDevices_ so it doesn't conflict
                            juce::ScopedLock lock(pluginLock_);
                            const auto devPath = ChainNodePath::topLevelDevice(trackId, devId);
                            auto sdIt = findSyncedDevice(devPath);
                            if (sdIt != syncedDevices_.end()) {
                                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(
                                        sdIt->second.plugin.get())) {
                                    dg->removeListener(this);
                                    removeDrumGridPadDevicesLocked(devPath);
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
                    const auto chainPath =
                        ChainNodePath::rack(trackId, rackInfo.id).withChain(chain.id);
                    for (const auto& chainElement : chain.elements) {
                        if (isDevice(chainElement)) {
                            const auto& device = getDevice(chainElement);
                            const auto devicePath = chainPath.withDevice(device.id);
                            auto* innerPlugin = rackSyncManager_.getInnerPlugin(device.id);
                            if (innerPlugin) {
                                juce::ScopedLock lock(pluginLock_);
                                auto& sd = syncedDevices_[devicePath];
                                sd.trackId = trackId;
                                sd.plugin = innerPlugin;
                                pluginToDevice_[innerPlugin] = devicePath;
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

    // Register DrumGrid pad plugins in syncedDevices_ before macro/mod sync.
    // Drum Grid device macros can target pad samplers, and those targets must
    // already be visible to PluginManager for the link resolver to bind them.
    {
        std::vector<std::pair<ChainNodePath, daw::audio::DrumGridPlugin*>> drumGrids;
        {
            juce::ScopedLock lock(pluginLock_);
            for (const auto& [devicePath, sd] : syncedDevices_) {
                if (sd.trackId != trackId)
                    continue;
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(sd.plugin.get()))
                    drumGrids.push_back({devicePath, dg});
            }
        }
        for (auto& [devicePath, dg] : drumGrids)
            syncDrumGridPadPlugins(devicePath, dg);
    }

    // Sync device-level + track-level modifiers AND macros via the
    // ModifierSyncWalker (issue #1131 step 2).
    syncDeviceModifiers(trackId, teTrack);

    // Update mod link fingerprint so resyncDeviceModifiers doesn't rebuild immediately after
    if (auto* info = TrackManager::getInstance().getTrack(trackId))
        modLinkFingerprints_[trackId] = computeModLinkFingerprint(trackId, info);

    // Sync sidechain routing for plugins that support it
    syncSidechains(trackId, teTrack);

    // MIDI sidechain monitors sit at the front so they see MIDI before instruments consume it.
    // Audio trigger monitors are reconciled after plugin ordering is stable below, because their
    // correct tap point can be after a source instrument/rack.
    if (trackNeedsSidechainMonitor(trackId))
        ensureSidechainMonitor(trackId);
    else
        removeSidechainMonitor(trackId);

    // Create TE plugins for post-FX devices (flat list, no racks/instruments).
    // Inserted at -1 (append); the reorder pass below places them after the fx
    // tree and ensureVolumePluginPosition keeps the fader after them, so the
    // final order is [fx..., postFx..., mixerAnalysis..., VolumeAndPan, LevelMeter].
    auto loadFlatSection = [&](const std::vector<PostFxChainElement>& section, auto pathBuilder) {
        for (const auto& elem : section) {
            const auto& device = elem.device;
            const auto devicePath = pathBuilder(trackId, device.id);
            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(devicePath) != syncedDevices_.end())
                continue;
            auto plugin = loadDeviceAsPlugin(devicePath, device, -1);
            if (!plugin)
                continue;
            auto& sd = syncedDevices_[devicePath];
            sd.trackId = trackId;
            sd.plugin = plugin;
            pluginToDevice_[plugin.get()] = devicePath;

            if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                if (extPlugin->isInitialisingAsync()) {
                    sd.isPendingLoad = true;
                    if (auto* devInfo =
                            TrackManager::getInstance().getDeviceInChainByPath(devicePath))
                        devInfo->loadState = DeviceLoadState::Loading;
                    TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
                    pollAsyncPluginLoad(devicePath, plugin);
                }
            }
        }
    };
    loadFlatSection(trackInfo->chain.postFxChainElements, &ChainNodePath::postFxDevice);
    loadFlatSection(trackInfo->chain.mixerAnalysisElements, &ChainNodePath::mixerAnalysisDevice);

    // Reorder TE plugins to match the MAGDA chain element order.
    // This handles moveNode (drag-and-drop reorder) where the MAGDA chain changed
    // but existing TE plugins haven't moved.
    bool pluginOrderChanged = false;
    {
        // Build the desired order of TE plugin indices from the MAGDA chain
        std::vector<te::Plugin*> desiredOrder;
        for (const auto& element : trackInfo->chain.fxChainElements) {
            if (isDevice(element)) {
                juce::ScopedLock lock(pluginLock_);
                const auto deviceId = getDevice(element).id;
                auto it = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, deviceId));
                if (it != syncedDevices_.end() && it->second.plugin) {
                    // For instrument-rack-wrapped plugins, find the rack instance on the track
                    auto* wrapped = instrumentRackManager_.getRackInstance(deviceId);
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

        // Post-FX devices come after the fx tree but before the fader; mixer-
        // analysis devices come after post-FX. Append both groups so the move
        // below sequences them last (the fader and meter are then pushed past
        // them by ensureVolumePluginPosition).
        auto appendSection = [&](const std::vector<PostFxChainElement>& section, auto pathBuilder) {
            for (const auto& elem : section) {
                const auto devicePath = pathBuilder(trackId, elem.device.id);
                juce::ScopedLock lock(pluginLock_);
                auto it = findSyncedDevice(devicePath);
                if (it != syncedDevices_.end() && it->second.plugin &&
                    teTrack->pluginList.indexOf(it->second.plugin.get()) >= 0)
                    desiredOrder.push_back(it->second.plugin.get());
            }
        };
        appendSection(trackInfo->chain.postFxChainElements, &ChainNodePath::postFxDevice);
        appendSection(trackInfo->chain.mixerAnalysisElements, &ChainNodePath::mixerAnalysisDevice);

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
                if (vtChildIdx != targetVtIdx) {
                    listState.moveChild(vtChildIdx, targetVtIdx, nullptr);
                    pluginOrderChanged = true;
                }
            } else {
                // Move after the previous desired plugin
                int prevVtIdx = listState.indexOf(desiredOrder[i - 1]->state);
                int curVtIdx = listState.indexOf(desiredOrder[i]->state);
                if (curVtIdx >= 0 && prevVtIdx >= 0 && curVtIdx != prevVtIdx + 1) {
                    listState.moveChild(curVtIdx, prevVtIdx + 1, nullptr);
                    pluginOrderChanged = true;
                }
            }
        }
    }

    // Device reordering above moves only MAGDA-visible plugins. MIDI sidechain
    // helper plugins must be snapped back around their target after that pass
    // so a sidechained FX receives source MIDI, then downstream devices receive
    // the original chain MIDI again.
    syncSidechains(trackId, teTrack);

    // Ensure VolumeAndPan is near the end of the chain (before LevelMeter)
    // This is the track's fader control - it should come AFTER audio sources
    ensureVolumePluginPosition(teTrack);

    // Ensure LevelMeter is at the end of the plugin chain for metering
    addLevelMeterToTrack(trackId);

    // TE restarts playback for plugin add/remove, but not for ValueTree child
    // order changes. After moveChild(), PluginList reports the new order while
    // the active playback graph can still process the previous order.
    if (pluginOrderChanged)
        requestPluginOrderGraphRestart(trackId, "track-plugin-order");

    // Rebuild trigger cache and reconcile audio trigger monitors after user plugin order is stable.
    refreshAudioSidechainMonitors();
}

// =============================================================================
// Track Deletion Cleanup
// =============================================================================

void PluginManager::cleanupTrackPlugins(TrackId trackId) {
    // 1. Collect DeviceIds belonging to this track using stored trackId
    std::vector<ChainNodePath> devicePaths;
    std::map<ChainNodePath, te::Plugin::Ptr> pluginsToDelete;
    std::vector<te::Plugin*> midiPluginsToDelete;
    auto* teTrack = trackController_.getAudioTrack(trackId);
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [devicePath, sd] : syncedDevices_) {
            if (sd.trackId != trackId)
                continue;

            devicePaths.push_back(devicePath);
            if (sd.plugin)
                pluginsToDelete[devicePath] = sd.plugin;
            if (sd.midiReceivePlugin)
                midiPluginsToDelete.push_back(sd.midiReceivePlugin.get());
            if (sd.midiRestorePlugin)
                midiPluginsToDelete.push_back(sd.midiRestorePlugin.get());
        }

        std::vector<te::Plugin*> scopePlugins;
        scopePlugins.reserve(devicePaths.size());
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (sd.trackId == trackId && sd.plugin)
                scopePlugins.push_back(sd.plugin.get());
        }

        auto* modifierList = teTrack ? teTrack->getModifierList() : nullptr;
        auto* macroList = teTrack ? &teTrack->getMacroParameterListForWriting() : nullptr;

        // 2. Erase map entries for collected DeviceIds
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (const auto& devicePath : devicePaths) {
            auto it = findSyncedDevice(devicePath);
            if (it != syncedDevices_.end()) {
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                teardownMacroMap(it->second.macroParams, it->second.modifiers, scopePlugins,
                                 macroList);
                teardownModifierMap(it->second.modifiers, scopePlugins, modifierList);

                // Clear LFO callbacks before destroying CurveSnapshotHolders
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
                    dg->removeListener(this);
                    removeDrumGridPadDevicesLocked(devicePath);
                }
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                syncedDevices_.erase(it);
            }
        }

        auto tmIt = trackModStates_.find(trackId);
        auto tmpIt = trackMacroParams_.find(trackId);
        if (tmpIt != trackMacroParams_.end()) {
            static const std::map<ModId, te::Modifier::Ptr> emptyModifiers;
            const auto& trackModifiers =
                tmIt != trackModStates_.end() ? tmIt->second.modifiers : emptyModifiers;
            teardownMacroMap(tmpIt->second, trackModifiers, scopePlugins, macroList);
        }
        if (tmIt != trackModStates_.end()) {
            clearLFOCustomWaveCallbacks(tmIt->second.modifiers);
            teardownModifierMap(tmIt->second.modifiers, scopePlugins, modifierList);
            deferCurveSnapshots(tmIt->second.curveSnapshots, deferredHolders_);
        }
    }

    // 3. Delete plugins and close windows outside lock
    for (size_t i = 0; i < devicePaths.size(); ++i) {
        const auto deviceId = devicePaths[i].getDeviceId();
        pluginWindowBridge_.closeWindowsForDevice(deviceId);

        // Unwrap instrument racks
        if (canOwnInstrumentWrapper(devicePaths[i]) &&
            instrumentRackManager_.getInnerPlugin(deviceId) != nullptr) {
            instrumentRackManager_.unwrap(deviceId);
        } else if (auto it = pluginsToDelete.find(devicePaths[i]); it != pluginsToDelete.end()) {
            if (it->second)
                it->second->deleteFromParent();
        }
    }
    for (auto* plugin : midiPluginsToDelete)
        if (plugin)
            plugin->deleteFromParent();

    // 4. Remove sidechain monitor for this track
    removeSidechainMonitor(trackId);

    // 5. Remove all racks belonging to this track
    rackSyncManager_.removeRacksForTrack(trackId);

    // 5b. Clean up track-level mod state
    {
        auto tmIt = trackModStates_.find(trackId);
        if (tmIt != trackModStates_.end()) {
            trackModStates_.erase(tmIt);
        }
    }

    // 5c. Clean up track-level macro state
    trackMacroParams_.erase(trackId);
    modLinkFingerprints_.erase(trackId);

    // 6. Clean up cross-track references (Stage 2)
    // Remove MidiReceivePlugins on other tracks that reference the deleted track as source
    {
        std::vector<ChainNodePath> midiReceiveToRemove;
        for (const auto& [devicePath, sd] : syncedDevices_) {
            if (sd.midiReceivePlugin) {
                if (auto* rx = dynamic_cast<MidiReceivePlugin*>(sd.midiReceivePlugin.get())) {
                    if (rx->getSourceTrackId() == trackId)
                        midiReceiveToRemove.push_back(devicePath);
                }
            }
            if (sd.midiRestorePlugin) {
                if (auto* rx = dynamic_cast<MidiReceivePlugin*>(sd.midiRestorePlugin.get())) {
                    if (rx->getSourceTrackId() == trackId)
                        midiReceiveToRemove.push_back(devicePath);
                }
            }
        }
        for (const auto& devicePath : midiReceiveToRemove) {
            auto it = findSyncedDevice(devicePath);
            if (it != syncedDevices_.end()) {
                auto* plugin = it->second.midiReceivePlugin.get();
                auto* restorePlugin = it->second.midiRestorePlugin.get();
                it->second.midiReceivePlugin = nullptr;
                it->second.midiRestorePlugin = nullptr;
                if (plugin)
                    plugin->deleteFromParent();
                if (restorePlugin)
                    restorePlugin->deleteFromParent();
            }
        }
    }

    // Clear audio sidechain sources on other tracks' plugins referencing deleted track
    {
        auto& tm = TrackManager::getInstance();
        for (const auto& track : tm.getTracks()) {
            if (track.id == trackId)
                continue;
            for (const auto& element : track.chain.fxChainElements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    if (device.sidechain.isActive() && device.sidechain.sourceTrackId == trackId) {
                        auto plugin = getPlugin(ChainNodePath::topLevelDevice(track.id, device.id));
                        if (plugin && plugin->canSidechain()) {
                            plugin->setSidechainSourceID({});
                        }
                    }
                } else if (isRack(element)) {
                    rackSyncManager_.syncSidechains(
                        getRack(element), [this](TrackId sourceTrackId) {
                            return trackController_.getAudioTrack(sourceTrackId);
                        });
                }
            }
        }
    }

    // 7. Rebuild sidechain LFO cache
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

    if (auto* spec = daw::audio::compiled::findCompiledPluginSpec(type)) {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, spec->pluginId, nullptr);
        plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (plugin)
            track->pluginList.insertPlugin(plugin, -1, nullptr);
    } else if (auto* spec = daw::audio::findInternalPluginSpecForLoadType(type)) {
        if (spec->canCreateOnTrack) {
            plugin = daw::audio::createInternalPluginFromSpec(*spec, edit_);
            if (plugin)
                track->pluginList.insertPlugin(plugin, -1, nullptr);
        }
    }

    if (!plugin)
        juce::Logger::writeToLog("Failed to create internal plugin '" + type + "' for track " +
                                 juce::String(trackId));

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

        // WORKAROUND for Tracktion Engine bug: When multiple plugins share the same
        // uniqueId (common in VST3 bundles with multiple components like Serum 2 + Serum 2 FX),
        // TE's findMatchingPlugin() matches by uniqueId first and returns the wrong plugin.
        // By clearing uniqueId, we force it to fall through to deprecatedUid matching,
        // which correctly distinguishes between plugins in the same bundle.
        juce::PluginDescription descCopy = description;
        if (descCopy.deprecatedUid != 0) {
            descCopy.uniqueId = 0;
        }

        // Create external plugin using the description
        auto plugin =
            edit_.getPluginCache().createNewPlugin(te::ExternalPlugin::xmlTypeName, descCopy);

        if (plugin) {
            // Check if plugin actually initialized successfully
            if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                // Debug: Check what plugin was actually created

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
            return PluginLoadResult::Success(plugin);
        } else {
            juce::String error = "Failed to create plugin: " + description.name;
            return PluginLoadResult::Failure(error);
        }
    } catch (const std::exception& e) {
        juce::String error = "Exception loading plugin " + description.name + ": " + e.what();
        return PluginLoadResult::Failure(error);
    } catch (...) {
        juce::String error = "Unknown exception loading plugin: " + description.name;
        return PluginLoadResult::Failure(error);
    }
}

te::Plugin::Ptr PluginManager::addLevelMeterToTrack(TrackId trackId) {
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track) {
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

void PluginManager::pollAsyncPluginLoad(const ChainNodePath& devicePath, te::Plugin::Ptr plugin) {
    auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get());
    if (!extPlugin)
        return;

    // Use a timer to poll until TE's async instantiation completes.
    // The timer runs on the message thread, same as TE's completion callback.
    // Capture a WeakReference to guard against PluginManager destruction.
    juce::WeakReference<PluginManager> weakThis(this);
    juce::Timer::callAfterDelay(100, [weakThis, devicePath, plugin]() {
        if (weakThis == nullptr)
            return;  // PluginManager was destroyed
        auto& self = *weakThis;
        const auto trackId = devicePath.trackId;
        const auto deviceId = devicePath.getDeviceId();

        auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get());
        if (!ext)
            return;

        // Check if device was removed while we were loading
        if (getDeviceInfoForPath(devicePath) == nullptr) {
            juce::ScopedLock lock(self.pluginLock_);
            if (auto sdIt = self.findSyncedDevice(devicePath); sdIt != self.syncedDevices_.end())
                sdIt->second.isPendingLoad = false;
            return;
        }

        if (ext->isInitialisingAsync()) {
            // Still loading — poll again
            self.pollAsyncPluginLoad(devicePath, plugin);
            return;
        }

        // Loading complete — update state
        {
            juce::ScopedLock lock(self.pluginLock_);
            if (auto sdIt = self.findSyncedDevice(devicePath); sdIt != self.syncedDevices_.end())
                sdIt->second.isPendingLoad = false;
        }

        bool loaded = ext->getLoadError().isEmpty();
        if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
            devInfo->loadState = loaded ? DeviceLoadState::Loaded : DeviceLoadState::Failed;
        }

        if (loaded) {
            // Apply bypass state
            plugin->setEnabled(true);
            if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
                plugin->setEnabled(!devInfo->bypassed);
            }

            // Apply an imported .vstpreset (DAWproject device state) now that the
            // VST3 instance is live; clear it once applied so a resync won't redo it.
            if (auto* devInfo = getDeviceInfoForPath(devicePath);
                devInfo && devInfo->vst3Preset.isNotEmpty()) {
                if (applyVst3Preset(plugin.get(), devInfo->vst3Preset))
                    devInfo->vst3Preset = {};
            }

            // Create processor now that the plugin instance is ready
            auto processor = createDeviceProcessorForPlugin(deviceId, plugin, {});

            // Populate parameters on the DeviceInfo
            if (processor) {
                if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
                    processor->populateParameters(*devInfo);

                    updateDeviceCapabilityFlags(*devInfo, *plugin);
                    AutoAliasGenerator::regenerateForDevice(devicePath);
                }
            }

            {
                juce::ScopedLock lock(self.pluginLock_);
                self.syncedDevices_[devicePath].processor = std::move(processor);
            }

            // Wrap instruments in a RackType (for audio passthrough + multi-out)
            if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
                if (devInfo->isInstrument) {
                    int numOutputChannels = ext->getNumOutputs();

                    // Remember the plugin's position before wrapping removes it
                    auto* track = self.trackController_.getAudioTrack(trackId);
                    int pluginIdx = track ? track->pluginList.indexOf(plugin.get()) : -1;

                    te::Plugin::Ptr rackPlugin;
                    if (numOutputChannels > 2) {
                        rackPlugin = self.instrumentRackManager_.wrapMultiOutInstrument(
                            plugin, numOutputChannels, devInfo->midiInThru);
                    } else {
                        rackPlugin =
                            self.instrumentRackManager_.wrapInstrument(plugin, devInfo->midiInThru);
                    }

                    if (rackPlugin) {
                        rackPlugin->setEnabled(!devInfo->bypassed);

                        // Insert the rack instance back at the original position
                        if (track)
                            track->pluginList.insertPlugin(rackPlugin, pluginIdx, nullptr);

                        auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
                        te::RackType::Ptr rackType = rackInstance ? rackInstance->type : nullptr;
                        self.instrumentRackManager_.recordWrapping(
                            devicePath, rackType, plugin, rackPlugin, numOutputChannels > 2,
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

    std::vector<ChainNodePath> magdaDevices;
    for (const auto& element : trackInfo.chain.fxChainElements) {
        if (isDevice(element))
            magdaDevices.push_back(ChainNodePath::topLevelDevice(trackId, getDevice(element).id));
    }
    for (const auto& postElem : trackInfo.chain.postFxChainElements)
        magdaDevices.push_back(ChainNodePath::postFxDevice(trackId, postElem.device.id));
    for (const auto& miniElem : trackInfo.chain.mixerAnalysisElements)
        magdaDevices.push_back(ChainNodePath::mixerAnalysisDevice(trackId, miniElem.device.id));

    std::vector<ChainNodePath> toRemove;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [devicePath, sd] : syncedDevices_) {
            if (!sd.plugin || sd.trackId != trackId)
                continue;

            const bool found = std::find(magdaDevices.begin(), magdaDevices.end(), devicePath) !=
                               magdaDevices.end();
            if (!found) {
                toRemove.push_back(devicePath);
                pluginsToDelete.push_back(sd.plugin);
            }
        }

        deferredHolders_.clear();
        std::vector<te::Plugin*> scopePlugins;
        for (const auto& [_devicePath, sd] : syncedDevices_) {
            if (sd.trackId == trackId && sd.plugin)
                scopePlugins.push_back(sd.plugin.get());
        }
        auto* modifierList = teTrack->getModifierList();
        auto* macroList = &teTrack->getMacroParameterListForWriting();
        for (const auto& devicePath : toRemove) {
            auto it = findSyncedDevice(devicePath);
            if (it == syncedDevices_.end())
                continue;

            clearLFOCustomWaveCallbacks(it->second.modifiers);
            teardownMacroMap(it->second.macroParams, it->second.modifiers, scopePlugins, macroList);
            teardownModifierMap(it->second.modifiers, scopePlugins, modifierList);
            deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
            if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
                dg->removeListener(this);
                removeDrumGridPadDevicesLocked(devicePath);
            }
            if (it->second.plugin)
                pluginToDevice_.erase(it->second.plugin.get());
            syncedDevices_.erase(it);
        }
    }

    for (size_t i = 0; i < toRemove.size(); ++i) {
        pluginWindowBridge_.closeWindowsForDevice(toRemove[i].getDeviceId());
        removeMidiReceive(toRemove[i]);
        if (pluginsToDelete[i])
            pluginsToDelete[i]->deleteFromParent();
    }

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
    for (size_t elemIdx = 0; elemIdx < trackInfo.chain.fxChainElements.size(); ++elemIdx) {
        const auto& element = trackInfo.chain.fxChainElements[elemIdx];
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            const auto devicePath = ChainNodePath::topLevelDevice(trackId, device.id);

            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(devicePath) == syncedDevices_.end()) {
                // Compute TE insertion index from subsequent synced devices
                int teInsertIndex = -1;
                for (size_t j = elemIdx + 1; j < trackInfo.chain.fxChainElements.size(); ++j) {
                    if (isDevice(trackInfo.chain.fxChainElements[j])) {
                        auto nextId = getDevice(trackInfo.chain.fxChainElements[j]).id;
                        auto it = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, nextId));
                        if (it != syncedDevices_.end() && it->second.plugin) {
                            int idx = teTrack->pluginList.indexOf(it->second.plugin.get());
                            if (idx >= 0) {
                                teInsertIndex = idx;
                                break;
                            }
                        }
                    }
                }

                auto plugin = loadDeviceAsPlugin(devicePath, device, teInsertIndex);
                if (plugin) {
                    auto& sd = syncedDevices_[devicePath];
                    sd.trackId = trackId;
                    sd.plugin = plugin;
                    pluginToDevice_[plugin.get()] = devicePath;
                }
            }
        }
    }

    auto loadFlatSection = [&](const std::vector<PostFxChainElement>& section, auto pathBuilder) {
        for (const auto& elem : section) {
            const auto& flatDevice = elem.device;
            const auto devicePath = pathBuilder(trackId, flatDevice.id);
            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(devicePath) != syncedDevices_.end())
                continue;
            auto plugin = loadDeviceAsPlugin(devicePath, flatDevice, -1);
            if (!plugin)
                continue;
            auto& sd = syncedDevices_[devicePath];
            sd.trackId = trackId;
            sd.plugin = plugin;
            pluginToDevice_[plugin.get()] = devicePath;

            if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                if (extPlugin->isInitialisingAsync()) {
                    sd.isPendingLoad = true;
                    if (auto* devInfo =
                            TrackManager::getInstance().getDeviceInChainByPath(devicePath))
                        devInfo->loadState = DeviceLoadState::Loading;
                    TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
                    pollAsyncPluginLoad(devicePath, plugin);
                }
            }
        }
    };
    loadFlatSection(trackInfo.chain.postFxChainElements, &ChainNodePath::postFxDevice);
    loadFlatSection(trackInfo.chain.mixerAnalysisElements, &ChainNodePath::mixerAnalysisDevice);

    // Reorder TE plugins to match the MAGDA chain element order (same as syncTrackPlugins)
    bool pluginOrderChanged = false;
    {
        std::vector<te::Plugin*> desiredOrder;
        for (const auto& element : trackInfo.chain.fxChainElements) {
            if (isDevice(element)) {
                juce::ScopedLock lock(pluginLock_);
                const auto deviceId = getDevice(element).id;
                auto it = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, deviceId));
                if (it != syncedDevices_.end() && it->second.plugin) {
                    if (teTrack->pluginList.indexOf(it->second.plugin.get()) >= 0)
                        desiredOrder.push_back(it->second.plugin.get());
                }
            }
        }
        auto appendSection = [&](const std::vector<PostFxChainElement>& section, auto pathBuilder) {
            for (const auto& elem : section) {
                const auto devicePath = pathBuilder(trackId, elem.device.id);
                juce::ScopedLock lock(pluginLock_);
                auto it = findSyncedDevice(devicePath);
                if (it != syncedDevices_.end() && it->second.plugin &&
                    teTrack->pluginList.indexOf(it->second.plugin.get()) >= 0)
                    desiredOrder.push_back(it->second.plugin.get());
            }
        };
        appendSection(trackInfo.chain.postFxChainElements, &ChainNodePath::postFxDevice);
        appendSection(trackInfo.chain.mixerAnalysisElements, &ChainNodePath::mixerAnalysisDevice);

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
                if (vtChildIdx != targetVtIdx) {
                    listState.moveChild(vtChildIdx, targetVtIdx, nullptr);
                    pluginOrderChanged = true;
                }
            } else {
                int prevVtIdx = listState.indexOf(desiredOrder[i - 1]->state);
                int curVtIdx = listState.indexOf(desiredOrder[i]->state);
                if (curVtIdx >= 0 && prevVtIdx >= 0 && curVtIdx != prevVtIdx + 1) {
                    listState.moveChild(curVtIdx, prevVtIdx + 1, nullptr);
                    pluginOrderChanged = true;
                }
            }
        }
    }

    // Ensure VolumeAndPan and LevelMeter are present
    ensureVolumePluginPosition(teTrack);
    addLevelMeterToTrack(trackId);

    if (pluginOrderChanged)
        requestPluginOrderGraphRestart(trackId, "multiout-plugin-order");

    // Set audio output routing (e.g. "track:N" to route back to parent)
    if (trackInfo.audioOutputDevice.isNotEmpty())
        trackController_.setTrackAudioOutput(trackId, trackInfo.audioOutputDevice);
}

// =============================================================================
// Master Channel Plugin Sync
// =============================================================================

void PluginManager::syncMasterPlugins() {
    auto* trackInfo = TrackManager::getInstance().getTrack(MASTER_TRACK_ID);
    if (!trackInfo)
        return;

    auto& masterList = edit_.getMasterPluginList();

    // Collect current MAGDA device paths on master (fx chain + flat post-fx list)
    std::vector<ChainNodePath> magdaDevices;
    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (isDevice(element))
            magdaDevices.push_back(
                ChainNodePath::topLevelDevice(MASTER_TRACK_ID, getDevice(element).id));
    }
    for (const auto& postElem : trackInfo->chain.postFxChainElements)
        magdaDevices.push_back(ChainNodePath::postFxDevice(MASTER_TRACK_ID, postElem.device.id));
    for (const auto& miniElem : trackInfo->chain.mixerAnalysisElements)
        magdaDevices.push_back(
            ChainNodePath::mixerAnalysisDevice(MASTER_TRACK_ID, miniElem.device.id));

    // Remove synced plugins that are no longer in MAGDA's master chain
    std::vector<ChainNodePath> toRemove;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [devicePath, sd] : syncedDevices_) {
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
                bool found = std::find(magdaDevices.begin(), magdaDevices.end(), devicePath) !=
                             magdaDevices.end();
                if (!found) {
                    toRemove.push_back(devicePath);
                    pluginsToDelete.push_back(sd.plugin);
                }
            }
        }
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (const auto& devicePath : toRemove) {
            auto it = findSyncedDevice(devicePath);
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
    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (!isDevice(element))
            continue;
        const auto& device = getDevice(element);
        const auto devicePath = ChainNodePath::topLevelDevice(MASTER_TRACK_ID, device.id);
        {
            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(devicePath) != syncedDevices_.end())
                continue;
        }

        auto plugin = createPluginOnly(MASTER_TRACK_ID, device);
        if (!plugin)
            continue;

        masterList.insertPlugin(plugin, -1, nullptr);
        if (masterList.indexOf(plugin.get()) < 0)
            continue;
        {
            juce::ScopedLock lock(pluginLock_);
            auto& sd = syncedDevices_[devicePath];
            sd.trackId = MASTER_TRACK_ID;
            sd.plugin = plugin;
            pluginToDevice_[plugin.get()] = devicePath;
        }

        // Create processor so UI parameter changes reach the TE plugin
        registerRackPluginProcessor(devicePath, plugin, device);

        // Update capability flags on the DeviceInfo
        if (auto* devInfo = TrackManager::getInstance().getDevice(MASTER_TRACK_ID, device.id)) {
            updateDeviceCapabilityFlags(*devInfo, *plugin);
        }

        // Handle async loading for external plugins
        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            if (extPlugin->isInitialisingAsync()) {
                juce::ScopedLock lock(pluginLock_);
                syncedDevices_[devicePath].isPendingLoad = true;
                if (auto* devInfo =
                        TrackManager::getInstance().getDevice(MASTER_TRACK_ID, device.id)) {
                    devInfo->loadState = DeviceLoadState::Loading;
                }
                TrackManager::getInstance().notifyTrackDevicesChanged(MASTER_TRACK_ID);
                pollAsyncPluginLoad(devicePath, plugin);
            }
        }
    }

    // Wire post-FX devices (flat list) after the fx inserts, so the master list
    // ends up [fx..., postFx...] ahead of the master fader.
    for (const auto& postElem : trackInfo->chain.postFxChainElements) {
        const auto& device = postElem.device;
        const auto postPath = ChainNodePath::postFxDevice(MASTER_TRACK_ID, device.id);
        {
            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(postPath) != syncedDevices_.end())
                continue;
        }

        auto plugin = createPluginOnly(MASTER_TRACK_ID, device);
        if (!plugin)
            continue;

        masterList.insertPlugin(plugin, -1, nullptr);
        if (masterList.indexOf(plugin.get()) < 0)
            continue;
        {
            juce::ScopedLock lock(pluginLock_);
            auto& sd = syncedDevices_[postPath];
            sd.trackId = MASTER_TRACK_ID;
            sd.plugin = plugin;
            pluginToDevice_[plugin.get()] = postPath;
        }

        registerRackPluginProcessor(postPath, plugin, device);

        // Post-fx devices are addressed by a post-fx path, not the top-level
        // getDevice() lookup used for fx devices above.
        if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(postPath)) {
            updateDeviceCapabilityFlags(*devInfo, *plugin);
        }

        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            if (extPlugin->isInitialisingAsync()) {
                juce::ScopedLock lock(pluginLock_);
                syncedDevices_[postPath].isPendingLoad = true;
                if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(postPath))
                    devInfo->loadState = DeviceLoadState::Loading;
                TrackManager::getInstance().notifyTrackDevicesChanged(MASTER_TRACK_ID);
                pollAsyncPluginLoad(postPath, plugin);
            }
        }
    }

    // Mixer-analysis devices: same shape as post-FX, sequenced after them.
    for (const auto& miniElem : trackInfo->chain.mixerAnalysisElements) {
        const auto& device = miniElem.device;
        const auto miniPath = ChainNodePath::mixerAnalysisDevice(MASTER_TRACK_ID, device.id);
        {
            juce::ScopedLock lock(pluginLock_);
            if (findSyncedDevice(miniPath) != syncedDevices_.end())
                continue;
        }

        auto plugin = createPluginOnly(MASTER_TRACK_ID, device);
        if (!plugin)
            continue;

        masterList.insertPlugin(plugin, -1, nullptr);
        if (masterList.indexOf(plugin.get()) < 0)
            continue;
        {
            juce::ScopedLock lock(pluginLock_);
            auto& sd = syncedDevices_[miniPath];
            sd.trackId = MASTER_TRACK_ID;
            sd.plugin = plugin;
            pluginToDevice_[plugin.get()] = miniPath;
        }

        registerRackPluginProcessor(miniPath, plugin, device);

        if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(miniPath)) {
            updateDeviceCapabilityFlags(*devInfo, *plugin);
        }

        if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
            if (extPlugin->isInitialisingAsync()) {
                juce::ScopedLock lock(pluginLock_);
                syncedDevices_[miniPath].isPendingLoad = true;
                if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(miniPath))
                    devInfo->loadState = DeviceLoadState::Loading;
                TrackManager::getInstance().notifyTrackDevicesChanged(MASTER_TRACK_ID);
                pollAsyncPluginLoad(miniPath, plugin);
            }
        }
    }
}

// =============================================================================
// Rack Plugin Creation
// =============================================================================

te::Plugin::Ptr PluginManager::createPluginOnly(TrackId trackId, const DeviceInfo& device) {
    juce::ignoreUnused(trackId);

    te::Plugin::Ptr plugin;

    if (device.format == PluginFormat::Internal) {
        const auto& ps = device.pluginState;

        if (auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(device.pluginId)) {
            plugin = createInternalPlugin(compiledSpec->pluginId, ps);
        } else if (auto* internalSpec = daw::audio::findInternalPluginSpec(device.pluginId)) {
            if (internalSpec->canCreateDetached)
                plugin = daw::audio::createInternalPluginFromSpec(*internalSpec, edit_, ps);

            // DrumGrid stores its inner chain state in pluginState as XML;
            // rehydrate it for detached/rack creation so pad assignments survive.
            if (plugin && internalSpec->kind == InternalDeviceKind::DrumGrid &&
                device.pluginState.isNotEmpty()) {
                if (auto xml = juce::XmlDocument::parse(device.pluginState)) {
                    auto savedState = juce::ValueTree::fromXml(*xml);
                    if (savedState.isValid())
                        plugin->restorePluginStateFromValueTree(savedState);
                }
            }
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

            // Final pass: by name + format only (other hosts' deviceRole is
            // unreliable, so isInstrument can't be required); resolves an imported
            // plugin to the installed one by name.
            if (!found) {
                for (const auto& knownDesc : knownPlugins.getTypes()) {
                    if (knownDesc.name == device.name &&
                        knownDesc.pluginFormatName == desc.pluginFormatName) {
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

void PluginManager::registerRackPluginProcessor(const ChainNodePath& devicePath,
                                                te::Plugin::Ptr plugin, const DeviceInfo& device) {
    const auto deviceId = devicePath.getDeviceId();
    if (!plugin)
        return;

    auto processor = createDeviceProcessorForPlugin(deviceId, plugin, device.pluginId);

    if (processor) {
        // Saved params (baseline) then native chunk (authoritative overlay) then
        // param-cache refresh -- one ordered op (same as loadDeviceAsPlugin).
        restoreDeviceStateWithChunkOverlay(*processor, plugin, device);

        // Populate processor-owned fields directly into the canonical
        // DeviceInfo. Snapshotting into a temp and copying only `.parameters`
        // back loses any other processor-populated field (wrapperParameters,
        // per-param displayText, etc.).
        if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath)) {
            processor->populateParameters(*devInfo);
        }
        AutoAliasGenerator::regenerateForDevice(devicePath);

        juce::ScopedLock lock(pluginLock_);
        syncedDevices_[devicePath].processor = std::move(processor);
    }
}

void PluginManager::refreshDeviceParameters(const ChainNodePath& devicePath) {
    DeviceProcessor* processor = nullptr;
    {
        juce::ScopedLock lock(pluginLock_);
        auto it = findSyncedDevice(devicePath);
        if (it == syncedDevices_.end()) {
            return;
        }
        if (it->second.processor == nullptr) {
            return;
        }
        processor = it->second.processor.get();
    }

    if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath)) {
        processor->populateParameters(*devInfo);
    }
    AutoAliasGenerator::regenerateForDevice(devicePath);
}

// =============================================================================
// Internal Implementation
// =============================================================================

te::Plugin::Ptr PluginManager::loadDeviceAsPlugin(const ChainNodePath& devicePath,
                                                  const DeviceInfo& device, int insertIndex) {
    const auto trackId = devicePath.trackId;
    auto* track = trackController_.getAudioTrack(trackId);
    if (!track)
        return nullptr;

    te::Plugin::Ptr plugin;
    std::unique_ptr<DeviceProcessor> processor;

    if (device.format == PluginFormat::Internal) {
        if (auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(device.pluginId)) {
            plugin = createInternalPlugin(compiledSpec->pluginId, device.pluginState);
            if (plugin)
                track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
        } else if (auto* internalSpec = daw::audio::findInternalPluginSpec(device.pluginId)) {
            if (internalSpec->canCreateOnTrack) {
                plugin = daw::audio::createInternalPluginFromSpec(*internalSpec, edit_,
                                                                  device.pluginState);
                if (plugin)
                    track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
            }

            if (plugin && internalSpec->kind == InternalDeviceKind::DrumGrid) {
                // DrumGrid: don't restore state here — defer until after rack
                // wrapping. Restoring adds PLUGIN children (samplers) to the
                // DrumGrid state, which confuses TE's rack graph builder.
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get()))
                    dg->addListener(this);
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

            auto& knownPlugins = engine_.getPluginManager().knownPluginList;

            // Debug: dump all plugins that match the name (case insensitive)
            for (const auto& kd : knownPlugins.getTypes()) {
                if (kd.name.containsIgnoreCase(device.name) ||
                    device.name.containsIgnoreCase(kd.name.toStdString())) {
                }
            }
            bool found = false;
            for (const auto& knownDesc : knownPlugins.getTypes()) {
                // Match by fileOrIdentifier (most specific) BUT also check isInstrument
                // to avoid loading FX when instrument is requested
                if (knownDesc.fileOrIdentifier == device.fileOrIdentifier &&
                    knownDesc.isInstrument == device.isInstrument) {
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
                        desc = knownDesc;
                        found = true;
                        break;
                    }
                }
            }

            // Final pass: match by name + format + isInstrument, ignoring vendor
            // and file path. DAWproject from other hosts (Bitwig) carries the
            // plugin name and the VST3 class id, but not MAGDA's file path or the
            // vendor, so the earlier passes can't resolve it; the name is the only
            // portable handle to the installed plugin.
            // Final pass: match by name + format only. Other hosts' deviceRole is
            // unreliable (Bitwig exports Serum, an instrument, as "audioFX"), so we
            // can't require isInstrument to agree; the name is the portable handle.
            if (!found) {
                for (const auto& knownDesc : knownPlugins.getTypes()) {
                    if (knownDesc.name == device.name &&
                        knownDesc.pluginFormatName == desc.pluginFormatName) {
                        desc = knownDesc;
                        found = true;
                        break;
                    }
                }
            }

            // Adopt the resolved plugin's instrument classification (the imported
            // deviceRole may be wrong), so MAGDA wraps/routes it correctly. A
            // DeviceType::MIDI device is an explicit MAGDA role override for
            // instrument-form MIDI generators, so preserve it.
            if (found && device.deviceType != DeviceType::MIDI) {
                if (auto* live = getDeviceInfoForPath(devicePath);
                    live && live->isInstrument != desc.isInstrument) {
                    live->isInstrument = desc.isInstrument;
                    live->deviceType =
                        desc.isInstrument ? DeviceType::Instrument : DeviceType::Effect;
                }
            }

            auto result = loadExternalPlugin(trackId, desc, insertIndex);
            if (result.success && result.plugin) {
                plugin = result.plugin;

                // Restore plugin native state (base64 blob) from DeviceInfo
                // For async plugins, TE reads the state property during init.
                // For sync plugins, we also call restorePluginStateFromValueTree().
                restorePluginState(devicePath, plugin);

                // If the plugin is loading asynchronously (TE background thread),
                // skip processor creation — it will be done in pollAsyncPluginLoad
                // when the VST instance is ready.
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                    if (ext->isInitialisingAsync()) {
                        return plugin;  // Return bare wrapper; async poll handles the rest
                    }
                    // Sync plugin already created — re-apply state now. (The
                    // authoritative restore + param-cache refresh happens after
                    // syncFromDeviceInfo below, where it can't be clobbered by the
                    // saved per-parameter array.)
                    if (device.pluginState.isNotEmpty()) {
                        ext->restorePluginStateFromValueTree(ext->state);
                    }
                    // Imported DAWproject .vstpreset state (instance is live here).
                    if (device.vst3Preset.isNotEmpty() && applyVst3Preset(ext, device.vst3Preset)) {
                        if (auto* devInfo = getDeviceInfoForPath(devicePath))
                            devInfo->vst3Preset = {};
                    }
                }

            } else {
                // Plugin failed to load - notify via callback
                if (onPluginLoadFailed) {
                    onPluginLoadFailed(device.id, result.errorMessage);
                }
                return nullptr;  // Don't proceed with a failed plugin
            }
        } else {
        }
    }

    if (plugin && !processor)
        processor = createDeviceProcessorForPlugin(device.id, plugin, device.pluginId);

    if (plugin) {
        // Update capability flags on the DeviceInfo in TrackManager
        if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
            updateDeviceCapabilityFlags(*devInfo, *plugin);
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
            // Saved params (baseline) then native chunk (authoritative overlay)
            // then param-cache refresh -- one ordered op, see helper.
            restoreDeviceStateWithChunkOverlay(*processor, plugin, device);

            // Populate processor-owned fields directly into the canonical
            // DeviceInfo (see comment in registerRackPluginProcessor).
            if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath)) {
                processor->populateParameters(*devInfo);
            }
            AutoAliasGenerator::regenerateForDevice(devicePath);

            syncedDevices_[devicePath].processor = std::move(processor);
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
                rackPlugin = instrumentRackManager_.wrapMultiOutInstrument(
                    plugin, numOutputChannels, device.midiInThru);
            } else {
                rackPlugin = instrumentRackManager_.wrapInstrument(plugin, device.midiInThru);
            }

            if (rackPlugin) {
                rackPlugin->setEnabled(!device.bypassed);

                // Insert the rack instance back on the track at the original position
                track->pluginList.insertPlugin(rackPlugin, pluginIdx, nullptr);

                // Record the wrapping so we can look up the inner plugin later
                auto* rackInstance = dynamic_cast<te::RackInstance*>(rackPlugin.get());
                te::RackType::Ptr rackType = rackInstance ? rackInstance->type : nullptr;
                instrumentRackManager_.recordWrapping(devicePath, rackType, plugin, rackPlugin,
                                                      numOutputChannels > 2, numOutputChannels);

                // Populate multi-out config on the DeviceInfo
                if (numOutputChannels > 2) {
                    // Populate MultiOutConfig on the DeviceInfo
                    if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
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
                    }
                }

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
        }

        // For tone generators (always transport-synced), sync initial state with transport
        if (auto* toneProc = syncedDevices_[devicePath].processor.get()) {
            if (auto* toneGen = dynamic_cast<ToneGeneratorProcessor*>(toneProc)) {
                // Get current transport state
                bool isPlaying = transportState_.isPlaying();
                // Bypass if transport is not playing
                toneGen->setBypassed(!isPlaying);
            }
        }

        // Note: Auto-routing MIDI for instruments is handled by AudioBridge
        // (coordination logic, not plugin management responsibility)
    }

    return plugin;
}

te::Plugin::Ptr PluginManager::createInternalPlugin(const juce::String& xmlTypeName,
                                                    const juce::String& savedPluginState) {
    if (savedPluginState.isNotEmpty()) {
        if (auto xml = juce::parseXML(savedPluginState)) {
            auto savedState = juce::ValueTree::fromXml(*xml);
            if (savedState.isValid()) {
                const auto savedType = savedState.getProperty(te::IDs::type).toString();
                if (savedPluginStateMatchesRequestedType(savedState, xmlTypeName)) {
                    stripTracktionIdsRecursive(savedState);
                    auto plugin = edit_.getPluginCache().createNewPlugin(savedState);
                    if (plugin)
                        return plugin;
                } else {
                }
            }
        } else {
        }
    }

    auto createFromValueTree = [&]() {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, xmlTypeName, nullptr);
        return edit_.getPluginCache().createNewPlugin(pluginState);
    };

    auto shouldUseTracktionStringFactory = [&]() {
        if (daw::audio::compiled::findCompiledPluginSpec(xmlTypeName) != nullptr)
            return false;

        const auto* spec = daw::audio::findInternalPluginSpecForLoadType(xmlTypeName);
        if (spec == nullptr)
            return true;

        switch (spec->kind) {
            case InternalDeviceKind::TeEq:
            case InternalDeviceKind::TeCompressor:
            case InternalDeviceKind::TeReverb:
            case InternalDeviceKind::TeDelay:
            case InternalDeviceKind::TeChorus:
            case InternalDeviceKind::TePhaser:
            case InternalDeviceKind::TeLowpass:
            case InternalDeviceKind::TePitchShift:
            case InternalDeviceKind::TeImpulseResponse:
            case InternalDeviceKind::TeVolumeAndPan:
            case InternalDeviceKind::TeFourOsc:
            case InternalDeviceKind::TeToneGenerator:
            case InternalDeviceKind::TeLevelMeter:
                return true;
            default:
                return false;
        }
    };

    te::Plugin::Ptr plugin;
    if (shouldUseTracktionStringFactory())
        plugin = edit_.getPluginCache().createNewPlugin(xmlTypeName, {});

    // For custom MAGDA plugins (analyzers, MIDI tools, etc.) the string overload
    // returns null and asserts in TE debug builds. The ValueTree overload routes
    // through createCustomPlugin.
    if (!plugin)
        plugin = createFromValueTree();

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
        ChainNodePath matchedPath;
        bool foundMatch = false;

        {
            juce::ScopedLock lock(self->pluginLock_);
            for (const auto& [devicePath, synced] : self->syncedDevices_) {
                const auto deviceId = devicePath.getDeviceId();
                if (synced.plugin.get() == dg ||
                    self->instrumentRackManager_.getInnerPlugin(deviceId) == dg) {
                    matchedPath = devicePath;
                    foundMatch = true;
                    break;
                }
            }
        }

        if (foundMatch) {
            self->syncDrumGridPadPlugins(matchedPath, dg);
            self->syncDrumGridMultiOutTracks(matchedPath, dg);
        }
    });
}

void PluginManager::syncDrumGridPadPlugins(const ChainNodePath& drumGridPath,
                                           daw::audio::DrumGridPlugin* drumGrid) {
    if (!drumGrid)
        return;

    const auto trackId = drumGridPath.trackId;
    const auto drumGridDeviceId = drumGridPath.getDeviceId();

    // Collect current valid pad plugin paths.
    std::set<ChainNodePath> currentPaths;
    for (const auto& chain : drumGrid->getChains()) {
        for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
            int devId = drumGrid->getPluginDeviceId(chain->index, pi);
            if (devId >= 0) {
                currentPaths.insert(
                    ChainNodePath::chainDevice(trackId, drumGridDeviceId, chain->index, devId));
            }
        }
    }

    juce::ScopedLock lock(pluginLock_);

    // Remove stale entries
    auto& oldPaths = drumGridPadDevices_[drumGridPath];
    for (const auto& oldPath : oldPaths) {
        if (currentPaths.find(oldPath) == currentPaths.end()) {
            auto it = findSyncedDevice(oldPath);
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
            const auto devicePath =
                ChainNodePath::chainDevice(trackId, drumGridDeviceId, chain->index, devId);
            if (findSyncedDevice(devicePath) == syncedDevices_.end()) {
                auto& sd = syncedDevices_[devicePath];
                sd.trackId = trackId;
                sd.plugin = chain->plugins[static_cast<size_t>(pi)];
                pluginToDevice_[sd.plugin.get()] = devicePath;
            }
        }
    }

    oldPaths = currentPaths;
}

void PluginManager::syncDrumGridMultiOutTracks(const ChainNodePath& drumGridPath,
                                               daw::audio::DrumGridPlugin* drumGrid) {
    const auto trackId = drumGridPath.trackId;
    const auto deviceId = drumGridPath.getDeviceId();
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
