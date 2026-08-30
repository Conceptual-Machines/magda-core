#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <vector>

#include "../../core/ChainRoutingModel.hpp"
#include "../../core/PluginCapabilities.hpp"
#include "../../core/RackInfo.hpp"
#include "../../core/TrackManager.hpp"
#include "../../core/aliases/AutoAliasGenerator.hpp"
#include "../../profiling/PerformanceProfiler.hpp"
#include "../PluginWindowBridge.hpp"
#include "../TrackController.hpp"
#include "../TracktionHelpers.hpp"
#include "ExternalPluginLookup.hpp"
#include "ExternalPluginStateUtil.hpp"
#include "PluginManager.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "modifiers/ModifierHelpers.hpp"
#include "modifiers/ModifierSync.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/AudioSidechainMonitorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/FaustPlugin.hpp"
#include "plugins/InsertCapturePlugin.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiDevicePlugin.hpp"
#include "plugins/MidiInThruSync.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"
#include "plugins/tracktion/TracktionDeviceStateBridge.hpp"
#include "plugins/tracktion/TracktionInternalPluginAdapter.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
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
        case PluginFormat::LV2:
            return "LV2";
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

    // The channel counts the chain model compiles against. Shared with the Drum
    // Grid's pad projection, which has to ask the same question the same way.
    applyLiveChannelCounts(device, plugin);
}

// Faust's processor owns a dynamic canSidechain flag, while the generic
// capability updater only ever promotes flags to true. Keep the type guard so
// a stale serialized flag on another plugin cannot erase valid routing.
// Return the id instead of changing TrackManager inline: its notification path
// must run after callers are finished with their borrowed DeviceInfo pointer.
DeviceId clearStaleFaustAudioSidechain(const DeviceInfo& device, te::Plugin* plugin) {
    auto* faust = daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::FaustPlugin>(plugin);
    if (faust == nullptr || !faust->activeDspMatchesSource() || device.canSidechain ||
        !device.sidechain.isActive() || device.sidechain.type != SidechainConfig::Type::Audio)
        return INVALID_DEVICE_ID;

    plugin->setSidechainSourceID({});
    return device.id;
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
    // Internal devices: seat the v2 document's frozen parameter values first, so
    // a device state that arrived without a matching DeviceInfo::parameters array
    // (a preset, an imported chain) still restores. syncFromDeviceInfo then has
    // the last word, keeping the model the authority for anything it does carry.
    if (plugin != nullptr && device.format == PluginFormat::Internal)
        daw::audio::tracktion_adapter::applyDeviceStateParameters(*plugin, device.pluginState);

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
                if (validDevicePaths.find(it->first) == validDevicePaths.end() &&
                    !isDrumGridPadPathLocked(it->first)) {
                    std::vector<te::Plugin*> scopePlugins;
                    for (const auto& [_deviceId, sd] : syncedDevices_) {
                        if (sd.trackId == it->second.trackId && sd.plugin)
                            scopePlugins.push_back(sd.plugin.get());
                    }
                    auto* teTrack = trackController_.getAudioTrack(it->second.trackId);
                    auto* modifierList = teTrack ? teTrack->getModifierList() : nullptr;
                    if (!modifierList && it->second.trackId == MASTER_TRACK_ID) {
                        if (auto* masterTrack = edit_.getMasterTrack())
                            modifierList = masterTrack->getModifierList();
                    }
                    auto* macroList =
                        teTrack ? &teTrack->getMacroParameterListForWriting() : nullptr;

                    clearLFOCustomWaveCallbacks(it->second.modifiers);
                    teardownMacroMap(it->second.macroParams, it->second.modifiers, scopePlugins,
                                     macroList);
                    teardownModifierMap(it->second.modifiers, scopePlugins, modifierList);
                    deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                    if (auto* dg =
                            dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
                        dg->removeListener(this);
                        // Pad entries are skipped by this sweep, so drop them with
                        // their owning DrumGrid. Only other map nodes are erased,
                        // so `it` stays valid.
                        removeDrumGridPadDevicesLocked(it->first);
                    }
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

            // Pad plugins live inside the DrumGrid's state, not in the chain
            // model. syncDrumGridPadPlugins owns their lifetime (#1920).
            if (isDrumGridPadPathLocked(devicePath))
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
                                devInfo.pluginState =
                                    daw::audio::tracktion_adapter::captureInternalDeviceState(
                                        *innerPlugin, devInfo.pluginState);
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

                // Register inner plugins in our device-to-plugin maps for
                // parameter access. Nested racks are walked too, the way
                // RackSyncManager loads them: a device inside one has an inner
                // plugin like any other, and without an entry here nothing that
                // works off syncedDevices_ can reach it. That included filling
                // a Drum Grid nested that deep from its pads (#2207).
                std::function<void(const RackInfo&, const ChainNodePath&)> registerInner =
                    [&](const RackInfo& rack, const ChainNodePath& rackPath) {
                        for (const auto& chain : rack.chains) {
                            const auto chainPath = rackPath.withChain(chain.id);
                            for (const auto& chainElement : chain.elements) {
                                if (isRack(chainElement)) {
                                    const auto& nested = getRack(chainElement);
                                    registerInner(nested, chainPath.withRack(nested.id));
                                    continue;
                                }

                                const auto& device = getDevice(chainElement);
                                const auto devicePath = chainPath.withDevice(device.id);
                                auto* innerPlugin = rackSyncManager_.getInnerPlugin(device.id);
                                if (innerPlugin == nullptr)
                                    continue;

                                juce::ScopedLock lock(pluginLock_);
                                auto& sd = syncedDevices_[devicePath];
                                sd.trackId = trackId;
                                sd.plugin = innerPlugin;
                                pluginToDevice_[innerPlugin] = devicePath;
                            }
                        }
                    };
                registerInner(rackInfo, ChainNodePath::rack(trackId, rackInfo.id));
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

    reconcileSends(*trackInfo, *teTrack);

    // Fill every Drum Grid on the track from its pads, then register the pad
    // plugins in syncedDevices_ before macro/mod sync. Drum Grid device macros
    // can target pad samplers, and those targets must already be visible to
    // PluginManager for the link resolver to bind them.
    for (auto& [devicePath, dg] : drumGridsOnTrack(trackId)) {
        syncDrumGridPads(devicePath, *dg);
        syncDrumGridPadPlugins(devicePath, dg);
    }

    // Sync device-level + track-level modifiers AND macros via the
    // ModifierSyncWalker (issue #1131 step 2).
    syncDeviceModifiers(trackId, teTrack->getModifierList(),
                        &teTrack->getMacroParameterListForWriting(),
                        [teTrack](const std::function<void(te::Plugin*)>& visit) {
                            for (int pi = 0; pi < teTrack->pluginList.size(); ++pi) {
                                if (auto* plugin = teTrack->pluginList[pi])
                                    visit(plugin);
                            }
                        });

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
    // Inserted at -1 (append); the reorder pass below sequences them, and which
    // side of VolumeAndPan they land on is TrackChain::postFxPostFader (#2087):
    //
    //   post-fader: [fx..., VolumeAndPan, postFx..., mixerAnalysis..., LevelMeter]
    //   pre-fader:  [fx..., postFx..., mixerAnalysis..., VolumeAndPan, LevelMeter]
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

        appendStripOrder(trackId, *trackInfo, *teTrack, desiredOrder);

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
                // A capture pass pins a hidden InsertCapturePlugin directly
                // after its insert; skip it so a mid-pass sync doesn't
                // displace it (and with it, the capture point).
                int expectedVtIdx = prevVtIdx + 1;
                while (expectedVtIdx < listState.getNumChildren()) {
                    auto child = listState.getChild(expectedVtIdx);
                    if (child.hasType(te::IDs::PLUGIN) &&
                        child.getProperty(te::IDs::type).toString() ==
                            InsertCapturePlugin::xmlTypeName)
                        ++expectedVtIdx;
                    else
                        break;
                }
                if (curVtIdx >= 0 && prevVtIdx >= 0 && curVtIdx != expectedVtIdx) {
                    listState.moveChild(curVtIdx, expectedVtIdx, nullptr);
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
    ensureVolumePluginPosition(trackId, teTrack);

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
            plugin = daw::audio::tracktion_adapter::createInternalPlugin(*spec, edit_);
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
            // Apply bypass state (device flag gated by the track chain power)
            plugin->setEnabled(true);
            if (auto* devInfo = getDeviceInfoForPath(devicePath)) {
                plugin->setEnabled(
                    TrackManager::getInstance().isDeviceEffectivelyEnabled(devicePath, *devInfo));
            }

            // Apply an imported .vstpreset (DAWproject device state) now that the
            // VST3 instance is live; clear it once applied so a resync won't redo it.
            if (auto* devInfo = getDeviceInfoForPath(devicePath);
                devInfo && devInfo->vst3Preset.isNotEmpty()) {
                if (applyVst3Preset(plugin.get(), devInfo->vst3Preset))
                    devInfo->vst3Preset = {};
            }

            // Create processor now that the plugin instance is ready
            auto processor =
                createDeviceProcessorForPlugin(deviceId, plugin, {}, &self.deviceTrackContext_);

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

                    const bool passRawMidi =
                        routing::makeRoutingNode(*devInfo).passesRawMidiInput();
                    te::Plugin::Ptr rackPlugin;
                    if (numOutputChannels > 2) {
                        rackPlugin = self.instrumentRackManager_.wrapMultiOutInstrument(
                            plugin, numOutputChannels, passRawMidi);
                    } else {
                        rackPlugin =
                            self.instrumentRackManager_.wrapInstrument(plugin, passRawMidi);
                    }

                    if (rackPlugin) {
                        rackPlugin->setEnabled(
                            TrackManager::getInstance().isDeviceEffectivelyEnabled(devicePath,
                                                                                   *devInfo));

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

void PluginManager::reconcileSends(const TrackInfo& trackInfo, te::AudioTrack& track) {
    // Make the track's AuxSendPlugins match TrackInfo::sends: drop the buses
    // that went away, create the ones that appeared, and refresh the levels.
    //
    // Shared with syncMultiOutTrack, which returns from syncTrackPlugins long
    // before this used to run. A multi-out track therefore had no send plugins
    // at all while the native compiler emitted its sends from the same model,
    // so the two engines disagreed about whether the sends existed - which
    // appendStripOrder could not show, because it can only order plugins that
    // are already there.
    std::vector<int> existingSendBuses;
    for (int i = 0; i < track.pluginList.size(); ++i)
        if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(track.pluginList[i]))
            existingSendBuses.push_back(auxSend->getBusNumber());

    std::vector<int> desiredBuses;
    desiredBuses.reserve(trackInfo.sends.size());
    for (const auto& send : trackInfo.sends)
        desiredBuses.push_back(send.busIndex);

    for (int i = track.pluginList.size() - 1; i >= 0; --i) {
        if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(track.pluginList[i])) {
            const int bus = auxSend->getBusNumber();
            if (std::find(desiredBuses.begin(), desiredBuses.end(), bus) == desiredBuses.end())
                auxSend->deleteFromParent();
        }
    }

    for (const auto& send : trackInfo.sends) {
        const bool exists = std::find(existingSendBuses.begin(), existingSendBuses.end(),
                                      send.busIndex) != existingSendBuses.end();
        if (exists)
            continue;
        auto sendPlugin =
            edit_.getPluginCache().createNewPlugin(te::AuxSendPlugin::xmlTypeName, {});
        if (!sendPlugin)
            continue;
        if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(sendPlugin.get())) {
            auxSend->busNumber = send.busIndex;
            auxSend->setGainDb(juce::Decibels::gainToDecibels(send.level));
        }
        // Appended; appendStripOrder sequences it onto the right side of the
        // fader afterwards.
        track.pluginList.insertPlugin(sendPlugin, -1, nullptr);
    }

    for (const auto& send : trackInfo.sends) {
        for (int i = 0; i < track.pluginList.size(); ++i) {
            if (auto* auxSend = dynamic_cast<te::AuxSendPlugin*>(track.pluginList[i]);
                auxSend != nullptr && auxSend->getBusNumber() == send.busIndex) {
                auxSend->setGainDb(juce::Decibels::gainToDecibels(send.level));
                break;
            }
        }
    }
}

void PluginManager::appendStripOrder(TrackId trackId, const TrackInfo& trackInfo,
                                     te::AudioTrack& track,
                                     std::vector<te::Plugin*>& desiredOrder) const {
    // Everything after the fx tree, in the order the native compiler emits it:
    //
    //   fx -> [postFx, mixerAnalysis if pre-fader] -> preFaderSends
    //      -> fader -> [postFx, mixerAnalysis if post-fader] -> postFaderSends
    //
    // The fader is not in this list; ensureVolumePluginPosition slots it in
    // afterwards, immediately before the first plugin that belongs after it.
    // That only lands correctly if everything pre-fader precedes everything
    // post-fader here, which is what the grouping below is for.
    //
    // Shared by syncTrackPlugins and syncMultiOutTrack because it was not:
    // the multi-out path carried its own copy that sequenced the stage and
    // nothing else, so a multi-out track got the fader placed against a set the
    // ordering pass had never arranged for.
    auto appendSection = [&](const std::vector<PostFxChainElement>& section, auto pathBuilder) {
        const juce::ScopedLock lock(pluginLock_);
        for (const auto& elem : section) {
            auto it = findSyncedDevice(pathBuilder(trackId, elem.device.id));
            if (it != syncedDevices_.end() && it->second.plugin &&
                track.pluginList.indexOf(it->second.plugin.get()) >= 0)
                desiredOrder.push_back(it->second.plugin.get());
        }
    };
    auto appendStage = [&] {
        appendSection(trackInfo.chain.postFxChainElements, &ChainNodePath::postFxDevice);
        appendSection(trackInfo.chain.mixerAnalysisElements, &ChainNodePath::mixerAnalysisDevice);
    };

    // Sends are sequenced here for the first time. They used to be appended and
    // left alone, which was harmless only because the fader was hoisted past
    // everything: every send ended up above it whatever SendInfo::preFader said,
    // so TE has never honoured that flag while the native compiler has always
    // split on it.
    auto appendSends = [&](bool preFader) {
        for (const auto& send : trackInfo.sends) {
            if (send.preFader != preFader)
                continue;
            for (int i = 0; i < track.pluginList.size(); ++i) {
                if (auto* aux = dynamic_cast<te::AuxSendPlugin*>(track.pluginList[i]);
                    aux != nullptr && aux->getBusNumber() == send.busIndex) {
                    desiredOrder.push_back(aux);
                    break;
                }
            }
        }
    };

    if (!trackInfo.chain.postFxPostFader)
        appendStage();
    appendSends(/*preFader=*/true);
    if (trackInfo.chain.postFxPostFader)
        appendStage();
    appendSends(/*preFader=*/false);
}

void PluginManager::ensureVolumePluginPosition(TrackId trackId, te::AudioTrack* track) const {
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

    // What legitimately belongs after the fader (#2087). Until now the answer
    // was "only LevelMeter", which is why the fader was hoisted past everything
    // else and why the post-FX stage could never actually be post-fader.
    const auto postFaderPlugins = collectPostFaderPlugins(trackId, *track);

    // Where the fader goes: immediately before the first plugin that belongs
    // after it, or at the end when nothing does.
    int firstPostFader = -1;
    for (int i = 0; i < plugins.size(); ++i) {
        if (plugins[i] == volPanRaw)
            continue;
        if (postFaderPlugins.count(plugins[i]) != 0) {
            firstPostFader = i;
            break;
        }
    }

    if (firstPostFader < 0) {
        // Nothing post-fader: the fader is last but for the meter, which is the
        // rule this function has always enforced.
        bool needsMove = false;
        for (int i = volPanIndex + 1; i < plugins.size(); ++i) {
            if (!dynamic_cast<te::LevelMeterPlugin*>(plugins[i])) {
                needsMove = true;
                break;
            }
        }
        if (!needsMove)
            return;

        // addLevelMeterToTrack() runs right after this and ensures LevelMeter is
        // always the very last plugin, so the final order will be:
        // [instruments, FX, sends, ..., VolumeAndPan, LevelMeter]
        volPanPlugin->removeFromParent();
        plugins.insertPlugin(volPanPlugin, -1, nullptr);
        return;
    }

    if (volPanIndex == firstPostFader - 1)
        return;  // already immediately before the post-fader run

    // Recompute the anchor after the removal rather than adjusting the index by
    // hand: removing the fader shifts everything after it down by one only when
    // the fader was earlier in the list, and getting that wrong puts the fader
    // one slot inside its own post-fader run.
    auto* anchor = plugins[firstPostFader];
    volPanPlugin->removeFromParent();
    const int insertAt = plugins.indexOf(anchor);
    plugins.insertPlugin(volPanPlugin, insertAt, nullptr);
}

std::unordered_set<te::Plugin*> PluginManager::collectPostFaderPlugins(
    TrackId trackId, te::AudioTrack& track) const {
    std::unordered_set<te::Plugin*> result;

    const auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (trackInfo == nullptr)
        return result;

    // The always-on measurement tap, which the mixing agent and MixAnalysisService
    // read. PluginManagerMeasurement appends it last and says "Post-fader" while
    // doing so; before this it was hoisted above the fader on the next chain sync,
    // so both of those were reading pre-fader audio. Included here rather than
    // left to the post-FX flag because it is post-fader unconditionally and its
    // placement should not depend on whether the user happens to own a post-FX
    // device.
    //
    // Unlocked, like every other access to this map: it is written and read on
    // the message thread only (see PluginManagerMeasurement.cpp). The lock below
    // is for syncedDevices_, which the async plugin-load path also touches.
    if (auto tap = trackMeasurementTaps_.find(trackId); tap != trackMeasurementTaps_.end()) {
        if (tap->second)
            result.insert(tap->second.get());
    }

    // A post-fader send taps the fader's output, which is what "post-fader"
    // means and what the native compiler has always done. TE never honoured the
    // flag because the fader was hoisted past every send regardless; it does now.
    for (const auto& send : trackInfo->sends) {
        if (send.preFader)
            continue;
        for (int i = 0; i < track.pluginList.size(); ++i)
            if (auto* aux = dynamic_cast<te::AuxSendPlugin*>(track.pluginList[i]);
                aux != nullptr && aux->getBusNumber() == send.busIndex)
                result.insert(aux);
    }

    if (!trackInfo->chain.postFxPostFader)
        return result;

    // One critical section for the whole walk rather than one per device.
    const juce::ScopedLock lock(pluginLock_);
    auto addSection = [&](const std::vector<PostFxChainElement>& section, auto pathBuilder) {
        for (const auto& elem : section) {
            auto it = findSyncedDevice(pathBuilder(trackId, elem.device.id));
            if (it != syncedDevices_.end() && it->second.plugin)
                result.insert(it->second.plugin.get());
        }
    };
    addSection(trackInfo->chain.postFxChainElements, &ChainNodePath::postFxDevice);
    addSection(trackInfo->chain.mixerAnalysisElements, &ChainNodePath::mixerAnalysisDevice);

    return result;
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

            // DrumGrid pad plugins are owned by syncDrumGridPadPlugins (#1920).
            if (isDrumGridPadPathLocked(devicePath))
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

    // The output pair's rack instance is what carries the multi-out routing.
    // A guarded block rather than the three early returns it used to be: none of
    // the sync below depends on the instance, so returning here gated a child
    // track's fx devices, post-FX stage, sends, ordering and fader placement on
    // multi-out plumbing that has nothing to do with any of them. A source
    // device that has not finished loading is enough to hit it, and the track
    // then had none of its chain (#2087 review).
    //
    // Declared out here because the ordering pass below anchors the first user
    // plugin after it when there is one.
    te::Plugin::Ptr rackInstance;
    if (auto* device =
            TrackManager::getInstance().getDevice(link.sourceTrackId, link.sourceDeviceId);
        device != nullptr && device->multiOut.isMultiOut && link.outputPairIndex >= 0 &&
        link.outputPairIndex < static_cast<int>(device->multiOut.outputPairs.size())) {
        const auto& outPair =
            device->multiOut.outputPairs[static_cast<size_t>(link.outputPairIndex)];

        // Nothing to repair. This track's own link is the assignment, so being
        // here already means the pair drives it; the sync used to write that
        // back onto the device because the device held a second copy that a
        // project load could not populate in time (#2220).
        rackInstance = instrumentRackManager_.createOutputInstance(
            link.sourceDeviceId, link.outputPairIndex, outPair.firstPin, outPair.numChannels);
        if (rackInstance) {
            bool alreadyOnTrack = false;
            for (int i = 0; i < teTrack->pluginList.size(); ++i) {
                if (teTrack->pluginList[i] == rackInstance.get()) {
                    alreadyOnTrack = true;
                    break;
                }
            }
            if (!alreadyOnTrack)
                teTrack->pluginList.insertPlugin(rackInstance, -1, nullptr);
        }
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

    // Sends, on the same terms as an ordinary track. Before ordering, because
    // appendStripOrder can only place plugins that already exist.
    reconcileSends(trackInfo, *teTrack);

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
        appendStripOrder(trackId, trackInfo, *teTrack, desiredOrder);

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
    ensureVolumePluginPosition(trackId, teTrack);
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
    auto* masterTrack = edit_.getMasterTrack();
    auto* masterModifierList = masterTrack ? masterTrack->getModifierList() : nullptr;

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
        std::vector<te::Plugin*> scopePlugins;
        scopePlugins.reserve(masterList.size());
        for (int i = 0; i < masterList.size(); ++i) {
            if (auto* plugin = masterList[i])
                scopePlugins.push_back(plugin);
        }
        for (const auto& devicePath : toRemove) {
            auto it = findSyncedDevice(devicePath);
            if (it != syncedDevices_.end()) {
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                teardownMacroMap(it->second.macroParams, it->second.modifiers, scopePlugins,
                                 nullptr);
                teardownModifierMap(it->second.modifiers, scopePlugins, masterModifierList);
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

    // The master owns a Tracktion ModifierList despite not being an
    // AudioTrack. Rebuild after plugins are mapped so Sidechain links can
    // resolve their master-device targets.
    resyncDeviceModifiers(MASTER_TRACK_ID);
}

// =============================================================================
// Rack Plugin Creation
// =============================================================================

te::Plugin::Ptr PluginManager::createPluginOnly(TrackId trackId, const DeviceInfo& device) {
    te::Plugin::Ptr plugin;

    if (device.format == PluginFormat::Internal) {
        const auto& ps = device.pluginState;

        if (auto* compiledSpec = daw::audio::compiled::findCompiledPluginSpec(device.pluginId)) {
            plugin = createInternalPlugin(compiledSpec->pluginId, ps);
        } else if (auto* internalSpec = daw::audio::findInternalPluginSpec(device.pluginId)) {
            if (internalSpec->canCreateDetached)
                plugin =
                    daw::audio::tracktion_adapter::createInternalPlugin(*internalSpec, edit_, ps);
        }

        // The device's own saved properties. Not every internal device seats
        // them at creation - a sampler is built fresh and reads its sample path
        // out of the tree it is restored with - and the parameter overlay a
        // caller applies afterwards carries only parameters. This is the same
        // call a track-level device gets from restorePluginState(), which is
        // where the two paths used to differ: a sampler on a track came back
        // with its sample and one inside a rack came back empty.
        if (plugin != nullptr && ps.isNotEmpty()) {
            namespace ta = daw::audio::tracktion_adapter;
            if (auto savedState = ta::devicePluginTreeFromState(ps); savedState.isValid()) {
                plugin->restorePluginStateFromValueTree(savedState);
                ta::applyDeviceStateParameters(*plugin, ps);
            }
        }
    } else {
        // External plugin. Which installed plugin the saved device meant is one
        // question with one answer (ExternalPluginLookup.hpp), asked here, by
        // the track path below, and by the native engine's device factory.
        if (device.uniqueId.isNotEmpty() || device.fileOrIdentifier.isNotEmpty()) {
            auto desc = matchInstalledPlugin(device, engine_.getPluginManager().knownPluginList)
                            .description;

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
        // Rack inner devices always live in the insert chain, so the chain
        // power gates them (no full path available here, only the track).
        plugin->setEnabled(!device.bypassed && TrackManager::getInstance().isChainEnabled(trackId));
        plugin->setDeltaSoloEnabled(device.deltaSolo);
    }

    return plugin;
}

// =============================================================================
// Rack Plugin Processor Registration
// =============================================================================

void PluginManager::registerRackPluginProcessor(const ChainNodePath& devicePath,
                                                te::Plugin::Ptr plugin, const DeviceInfo& device) {
    registerRackPluginProcessor(devicePath, plugin, device,
                                TrackManager::getInstance().getDeviceInChainByPath(devicePath));
}

void PluginManager::registerRackPluginProcessor(const ChainNodePath& devicePath,
                                                te::Plugin::Ptr plugin, const DeviceInfo& device,
                                                DeviceInfo* canonical) {
    const auto deviceId = devicePath.getDeviceId();
    if (!plugin)
        return;

    // What the plugin reports, onto the canonical DeviceInfo. Before the
    // processor below, since this has to run even when no processor could be
    // made: the counts come off the plugin, not off the processor. Every
    // rack-contained device passes through here and nowhere else, so without
    // this they would all keep the stereo defaults.
    if (canonical != nullptr)
        updateDeviceCapabilityFlags(*canonical, *plugin);

    auto processor =
        createDeviceProcessorForPlugin(deviceId, plugin, device.pluginId, &deviceTrackContext_);

    if (processor) {
        // Saved params (baseline) then native chunk (authoritative overlay) then
        // param-cache refresh -- one ordered op (same as loadDeviceAsPlugin).
        restoreDeviceStateWithChunkOverlay(*processor, plugin, device);

        // Populate processor-owned fields directly into the canonical
        // DeviceInfo. Snapshotting into a temp and copying only `.parameters`
        // back loses any other processor-populated field (wrapperParameters,
        // per-param displayText, etc.).
        if (canonical != nullptr)
            processor->populateParameters(*canonical);
        AutoAliasGenerator::regenerateForDevice(devicePath);

        juce::ScopedLock lock(pluginLock_);
        syncedDevices_[devicePath].processor = std::move(processor);
    }
}

void PluginManager::refreshDeviceParameters(const ChainNodePath& devicePath) {
    DeviceProcessor* processor = nullptr;
    te::Plugin::Ptr plugin;
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
        plugin = it->second.plugin;
    }

    DeviceId sidechainToClear = INVALID_DEVICE_ID;
    if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath)) {
        processor->populateParameters(*devInfo);
        sidechainToClear = clearStaleFaustAudioSidechain(*devInfo, plugin.get());
    }
    if (sidechainToClear != INVALID_DEVICE_ID)
        TrackManager::getInstance().clearSidechain(sidechainToClear);

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
                plugin = daw::audio::tracktion_adapter::createInternalPlugin(*internalSpec, edit_,
                                                                             device.pluginState);
                if (plugin)
                    track->pluginList.insertPlugin(plugin, insertIndex, nullptr);
            }

            if (plugin && daw::audio::internalPluginHasTag(*internalSpec, "drum-grid")) {
                // DrumGrid: don't restore state here — defer until after rack
                // wrapping. Restoring adds PLUGIN children (samplers) to the
                // DrumGrid state, which confuses TE's rack graph builder.
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get()))
                    dg->addListener(this);
            }
        }
    } else {
        // External plugin. The same lookup the rack path above asks, and the one
        // the native engine asks (ExternalPluginLookup.hpp).
        if (device.uniqueId.isNotEmpty() || device.fileOrIdentifier.isNotEmpty()) {
            const auto match =
                matchInstalledPlugin(device, engine_.getPluginManager().knownPluginList);
            auto desc = match.description;
            const bool found = match.found;

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
        processor = createDeviceProcessorForPlugin(device.id, plugin, device.pluginId,
                                                   &deviceTrackContext_);

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
            DeviceId sidechainToClear = INVALID_DEVICE_ID;
            if (auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath)) {
                processor->populateParameters(*devInfo);
                sidechainToClear = clearStaleFaustAudioSidechain(*devInfo, plugin.get());
            }

            syncedDevices_[devicePath].processor = std::move(processor);
            if (sidechainToClear != INVALID_DEVICE_ID)
                TrackManager::getInstance().clearSidechain(sidechainToClear);

            AutoAliasGenerator::regenerateForDevice(devicePath);
        }

        // Apply device state (device flag gated by the track chain power)
        plugin->setEnabled(
            TrackManager::getInstance().isDeviceEffectivelyEnabled(devicePath, device));
        daw::audio::syncPluginMidiInThru(plugin.get(), device.midiInThru);

        // Wrap instruments in a RackType with audio passthrough so both synth
        // output and audio clips on the same track are summed together.
        //
        // Exception: an External Instrument is a te::InsertPlugin. TE only turns
        // an InsertPlugin into a graph-level send/return when it sits DIRECTLY in
        // the track's plugin list (EditNodeBuilder special-case). Inside a
        // RackType it would be processed as a normal plugin and hit
        // InsertPlugin::applyToBuffer's jassertfalse (dead stub), with no audio
        // routed. So never wrap it, even though it presents as an instrument.
        const bool isExternalInsert =
            daw::audio::internalPluginHasTag(device.pluginId, "external-insert");
        if (device.isInstrument && !isExternalInsert) {
            // Detect multi-output capability.
            //
            // The two named types answer through their own API, and everything
            // else is asked the question every te::Plugin already answers: how
            // many channels it writes given the two it is offered. That last
            // branch used to not exist, so an internal instrument with more
            // than two outputs was wrapped as stereo and its further pins never
            // existed -- a type test standing in for a capability query. It is
            // inert for the fleet as it stands, since every internal instrument
            // answers two, and it is what lets one that does not be wrapped for
            // what it is (#2174).
            int numOutputChannels = 2;
            if (auto* extPlugin = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                numOutputChannels = extPlugin->getNumOutputs();
            } else if (auto* drumGrid = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin.get())) {
                numOutputChannels = drumGrid->getNumOutputChannels();
            } else {
                numOutputChannels = std::max(2, plugin->getNumOutputChannelsGivenInputs(2));
            }

            // Remember the plugin's position before wrapping removes it from the track
            int pluginIdx = track->pluginList.indexOf(plugin.get());

            const bool passRawMidi = routing::makeRoutingNode(device).passesRawMidiInput();
            te::Plugin::Ptr rackPlugin;
            if (numOutputChannels > 2) {
                rackPlugin = instrumentRackManager_.wrapMultiOutInstrument(
                    plugin, numOutputChannels, passRawMidi);
            } else {
                rackPlugin = instrumentRackManager_.wrapInstrument(plugin, passRawMidi);
            }

            if (rackPlugin) {
                rackPlugin->setEnabled(
                    TrackManager::getInstance().isDeviceEffectivelyEnabled(devicePath, device));

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
                if ((device.pluginId.containsIgnoreCase(daw::audio::DrumGridPlugin::xmlTypeName) ||
                     device.pluginId.containsIgnoreCase(
                         daw::audio::MagdaSamplerPlugin::xmlTypeName)) &&
                    device.pluginState.isNotEmpty()) {
                    auto savedState = daw::audio::tracktion_adapter::devicePluginTreeFromState(
                        device.pluginState);
                    if (savedState.isValid())
                        plugin->restorePluginStateFromValueTree(savedState);
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
    if (const auto* spec = daw::audio::findInternalPluginSpecForLoadType(xmlTypeName))
        return daw::audio::tracktion_adapter::createInternalPlugin(*spec, edit_, savedPluginState);

    if (savedPluginState.isNotEmpty()) {
        auto savedState =
            daw::audio::tracktion_adapter::devicePluginTreeFromState(savedPluginState);
        if (savedState.isValid() && savedPluginStateMatchesRequestedType(savedState, xmlTypeName)) {
            if (auto plugin = edit_.getPluginCache().createNewPlugin(savedState)) {
                daw::audio::tracktion_adapter::applyDeviceStateParameters(*plugin,
                                                                          savedPluginState);
                return plugin;
            }
        }
    }

    auto createFromValueTree = [&]() {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, xmlTypeName, nullptr);
        return edit_.getPluginCache().createNewPlugin(pluginState);
    };

    te::Plugin::Ptr plugin;
    if (daw::audio::compiled::findCompiledPluginSpec(xmlTypeName) == nullptr)
        plugin = edit_.getPluginCache().createNewPlugin(xmlTypeName, {});

    // For custom MAGDA plugins (analyzers, MIDI tools, etc.) the string overload
    // returns null and asserts in TE debug builds. The ValueTree overload routes
    // through createCustomPlugin.
    if (!plugin)
        plugin = createFromValueTree();

    return plugin;
}

//==============================================================================
// DrumGrid multi-out track sync
//==============================================================================

void PluginManager::drumGridChainsChanged(daw::audio::DrumGridPlugin* plugin) {
    if (!plugin)
        return;

    // Which device this is, resolved now rather than carried. The async below
    // used to capture a te::Plugin::Ptr to keep the plugin alive across the
    // call, and that reference is the thing that has to go.
    //
    // A te::Plugin may not outlive its Edit: its destructor reaches
    // edit.getParameterChangeHandler(), and AutomatableEditItem's teardown
    // takes a lock on it inside a noexcept function, so a plugin destroyed
    // after its Edit terminates the process rather than crashing somewhere a
    // stack trace explains. A message posted here and delivered after the Edit
    // is gone -- a project closed, or a render finished, with a pad load still
    // in flight -- makes the lambda's own captured reference the last one, and
    // the plugin then dies on the message thread with nothing left to reach.
    //
    // Nothing is captured that can keep it alive, so what arrives late finds
    // the device gone and does nothing. The path is looked up here because the
    // caller is on the thread that knows the plugin is alive.
    //
    // Looked up two ways for the same reason the async did: a drum grid inside
    // a rack is not the plugin its device path holds, and the rack manager is
    // what knows the inner one.
    ChainNodePath matchedPath;
    bool foundMatch = false;

    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [devicePath, synced] : syncedDevices_) {
            const auto deviceId = devicePath.getDeviceId();
            if (synced.plugin.get() == plugin ||
                instrumentRackManager_.getInnerPlugin(deviceId) == plugin) {
                matchedPath = devicePath;
                foundMatch = true;
                break;
            }
        }
    }

    if (!foundMatch)
        return;

    // Dispatch asynchronously — this callback fires during loadSampleToPad/addChain,
    // and synchronous track activation would re-entrantly destroy UI components
    // (e.g., DeviceSlotComponent) while their callbacks are still on the stack.
    juce::WeakReference<PluginManager> weakThis(this);

    juce::MessageManager::callAsync([weakThis, matchedPath]() {
        auto* self = weakThis.get();
        if (!self)
            return;

        // Resolved again, under the lock, because everything this message says
        // about the device was true when it was posted and none of it is
        // guaranteed now. A device removed in between resolves to nothing and
        // the message does nothing, which is the whole point of not carrying a
        // reference to it.
        //
        // The lock is released before the sync calls below: syncDrumGridMultiOutTracks
        // reaches TrackManager listeners, which reach syncAllPlugins, which takes
        // pluginLock_ again.
        daw::audio::DrumGridPlugin* dg = nullptr;

        {
            juce::ScopedLock lock(self->pluginLock_);
            const auto found = self->findSyncedDevice(matchedPath);
            if (found == self->syncedDevices_.end())
                return;

            dg = dynamic_cast<daw::audio::DrumGridPlugin*>(found->second.plugin.get());
            if (dg == nullptr)
                dg = dynamic_cast<daw::audio::DrumGridPlugin*>(
                    self->instrumentRackManager_.getInnerPlugin(matchedPath.getDeviceId()));
        }

        if (dg == nullptr)
            return;

        self->syncDrumGridPadPlugins(matchedPath, dg);
        self->syncDrumGridMultiOutTracks(matchedPath, dg);
    });
}

std::vector<std::pair<ChainNodePath, daw::audio::DrumGridPlugin*>> PluginManager::drumGridsOnTrack(
    TrackId trackId) {
    std::vector<std::pair<ChainNodePath, daw::audio::DrumGridPlugin*>> drumGrids;

    juce::ScopedLock lock(pluginLock_);
    for (const auto& [devicePath, sd] : syncedDevices_) {
        if (sd.trackId != trackId)
            continue;
        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(sd.plugin.get()))
            drumGrids.push_back({devicePath, dg});
    }

    return drumGrids;
}

void PluginManager::syncDrumGridPads(const ChainNodePath& drumGridPath,
                                     daw::audio::DrumGridPlugin& drumGrid) {
    auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(drumGridPath);
    if (devInfo == nullptr)
        return;

    // A grid with no pads yet still syncs: that is how a pad the model dropped
    // leaves the engine, and how a Drum Grid that has just been added starts
    // out empty rather than with whatever its plugin state happened to carry.
    static const RackInfo kNoPads;
    const RackInfo& pads = devInfo->pads ? *devInfo->pads.get() : kNoPads;

    const auto trackId = drumGridPath.trackId;
    drumGrid.syncFromModel(pads, [this, trackId](const DeviceInfo& padDevice) {
        return createPluginOnly(trackId, padDevice);
    });
}

void PluginManager::captureDrumGridPads(const ChainNodePath& drumGridPath,
                                        daw::audio::DrumGridPlugin& drumGrid) {
    // A pad's patch comes back off its own plugin, the same as a track
    // device's. Done here rather than by the loop over synced devices, because
    // a pad device is reached through the grid that owns it and not by its
    // path: the path's rack component is a DeviceId, which a Rack step cannot
    // tell from a rack of the same number (#2207).
    //
    // What pads exist and what sits on them travels the other way and is never
    // read back.
    auto& trackManager = TrackManager::getInstance();

    for (const auto& chain : drumGrid.getChains()) {
        if (chain == nullptr)
            continue;

        auto* pad = trackManager.getPadChain(drumGridPath, chain->index);
        if (pad == nullptr)
            continue;

        for (int i = 0; i < static_cast<int>(chain->plugins.size()); ++i) {
            const auto deviceId = drumGrid.getPluginDeviceId(chain->index, i);
            const auto& plugin = chain->plugins[static_cast<std::size_t>(i)];
            if (deviceId == INVALID_DEVICE_ID || plugin == nullptr)
                continue;

            for (auto& element : pad->elements) {
                if (!isDevice(element) || getDevice(element).id != deviceId)
                    continue;

                auto& padDevice = getDevice(element);
                if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
                    ext->flushPluginStateToValueTree();
                    padDevice.pluginState = ext->state.getProperty(te::IDs::state).toString();
                } else {
                    padDevice.pluginState =
                        daw::audio::tracktion_adapter::captureInternalDeviceState(
                            *plugin, padDevice.pluginState);
                }

                // And its parameters, as every other captured device gets. The
                // model's values are seated back onto the plugin when it is
                // rebuilt, so leaving them at what creation reported would put
                // the defaults over what the patch just saved.
                {
                    juce::ScopedLock lock(pluginLock_);
                    const auto padPath =
                        TrackManager::padChainPath(drumGridPath, chain->index).withDevice(deviceId);
                    if (auto padIt = findSyncedDevice(padPath);
                        padIt != syncedDevices_.end() && padIt->second.processor != nullptr)
                        padIt->second.processor->populateParameters(padDevice);
                }
                break;
            }
        }
    }
}

void PluginManager::syncDrumGridPadPlugins(const ChainNodePath& drumGridPath,
                                           daw::audio::DrumGridPlugin* drumGrid) {
    if (!drumGrid)
        return;

    const auto trackId = drumGridPath.trackId;

    // Every live pad plugin, keyed by the model path of the device it was built
    // for. A real path, not an invented one: it resolves through the pad rack
    // the device owns, so a pad plugin is reached by capture, by macro and mod
    // linking and by the parameter refresh exactly as a rack device is (#2207).
    std::map<ChainNodePath, te::Plugin::Ptr> current;
    for (const auto& chain : drumGrid->getChains()) {
        if (chain == nullptr)
            continue;
        const auto chainPath = TrackManager::padChainPath(drumGridPath, chain->index);
        for (int pi = 0; pi < static_cast<int>(chain->plugins.size()); ++pi) {
            const int devId = drumGrid->getPluginDeviceId(chain->index, pi);
            if (devId >= 0)
                current[chainPath.withDevice(devId)] = chain->plugins[static_cast<size_t>(pi)];
        }
    }

    std::vector<ChainNodePath> added;
    {
        juce::ScopedLock lock(pluginLock_);

        // Remove stale entries
        auto& oldPaths = drumGridPadDevices_[drumGridPath];
        for (const auto& oldPath : oldPaths) {
            if (current.find(oldPath) == current.end()) {
                auto it = findSyncedDevice(oldPath);
                if (it != syncedDevices_.end()) {
                    if (it->second.plugin)
                        pluginToDevice_.erase(it->second.plugin.get());
                    syncedDevices_.erase(it);
                }
            }
        }

        oldPaths.clear();
        for (const auto& [devicePath, plugin] : current) {
            oldPaths.insert(devicePath);
            if (findSyncedDevice(devicePath) != syncedDevices_.end())
                continue;

            auto& sd = syncedDevices_[devicePath];
            sd.trackId = trackId;
            sd.plugin = plugin;
            pluginToDevice_[plugin.get()] = devicePath;
            added.push_back(devicePath);
        }
    }

    // Outside the lock, because it reaches into TrackManager. A processor is
    // what seats the model's parameter values on the plugin and reads back what
    // only the plugin can answer: a parameter's name and range, and the channel
    // counts the plan compiler sizes the pad's ports with.
    //
    // The pad device is found through the grid that owns it, not through the
    // path above: a pad path's rack component is a DeviceId, which a Rack step
    // cannot tell from a rack of the same number (#2207). The path is the
    // registration key and the address a stored link carries, nothing more.
    for (const auto& devicePath : added)
        if (auto* padDevice = padDeviceFor(drumGridPath, devicePath))
            registerRackPluginProcessor(devicePath, current[devicePath], *padDevice, padDevice);
}

DeviceInfo* PluginManager::padDeviceFor(const ChainNodePath& drumGridPath,
                                        const ChainNodePath& padDevicePath) {
    auto& trackManager = TrackManager::getInstance();
    auto* pad = trackManager.getPadChain(drumGridPath, padDevicePath.getChainId());
    if (pad == nullptr)
        return nullptr;

    const auto deviceId = padDevicePath.getDeviceId();
    for (auto& element : pad->elements)
        if (isDevice(element) && getDevice(element).id == deviceId)
            return &getDevice(element);

    return nullptr;
}

void PluginManager::syncDrumGridMultiOutTracks(const ChainNodePath& drumGridPath,
                                               daw::audio::DrumGridPlugin* drumGrid) {
    const auto trackId = drumGridPath.trackId;
    const auto deviceId = drumGridPath.getDeviceId();
    auto& tm = TrackManager::getInstance();

    // By path, not by (track, device): the flat lookup misses a grid nested in
    // a rack, which is a placement the model supports and the pad bus selector
    // can now reach (#2211).
    auto* devInfo = tm.getDeviceInChainByPath(drumGridPath);
    if (!devInfo || !devInfo->multiOut.isMultiOut)
        return;

    // A grid can arrive here in a placement where buses do not work: wrapping a
    // top-level one in a rack moves it and keeps its pads, and a project can be
    // loaded already like that. Nothing carries a bus off a nested grid, so a
    // pad left on one would simply go silent, and any child track it had made
    // would linger with nothing feeding it. Put the pads back on the grid's own
    // mix and take the children down.
    if (!tm.padBusesAvailable(drumGridPath)) {
        tm.resetPadBuses(drumGridPath);
        tm.deactivateAllMultiOutPairs(trackId, deviceId);
        return;
    }

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
        if (tm.multiOutPairIsActive(trackId, deviceId, p) &&
            activeBuses.find(p) == activeBuses.end()) {
            tm.deactivateMultiOutPair(trackId, deviceId, p);
        }
    }

    // Activate pairs for chains that need them
    for (int bus : activeBuses) {
        if (bus >= static_cast<int>(pairs.size()))
            continue;

        auto& pair = pairs[static_cast<size_t>(bus)];
        if (!tm.multiOutPairIsActive(trackId, deviceId, bus)) {
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
        } else if (const auto childTrackId = tm.multiOutChildTrack(trackId, deviceId, bus);
                   childTrackId != INVALID_TRACK_ID) {
            // Update name if chain name changed
            auto it = busNames.find(bus);
            if (it != busNames.end()) {
                auto newName = drumGrid->getName() + ": " + it->second;
                if (auto* childTrack = tm.getTrack(childTrackId)) {
                    if (childTrack->name != newName) {
                        tm.setTrackName(childTrackId, newName);
                        pair.name = it->second;
                    }
                }
            }
        }
    }
}

}  // namespace magda
