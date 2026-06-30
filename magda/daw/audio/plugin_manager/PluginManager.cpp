#include "PluginManager.hpp"

#include <algorithm>
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
#include "../Vst3Preset.hpp"
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
#include "processors/DeviceProcessor.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

namespace {

// Resolve the TE param for a macro/mod link whose target is another modifier
// on the same scope (device or track). modParamIndex 0 == Rate; we pick the
// LFO's `rate` param when tempoSync is off and `rateType` when it's on, so
// the link drives whichever param the unified Rate lane writes to.
//
// `scopeMods` is the MAGDA ModInfo vector for the scope the source lives on
// (device.mods or trackInfo->mods); `scopeTeMods` is the matching ModId-keyed
// TE modifier map (syncedDevices_[id].modifiers or trackModStates_[id].modifiers).
// Same-scope only — cross-device / cross-rack mod targeting isn't supported
// yet; return nullptr if the modId isn't found in this scope.
template <typename Link>
te::AutomatableParameter* resolveSameScopeModParam(
    const Link& link, const std::vector<ModInfo>& scopeMods,
    const std::map<ModId, te::Modifier::Ptr>& scopeTeMods) {
    if (link.target.kind != decltype(link.target.kind)::ModParam)
        return nullptr;

    auto teIt = scopeTeMods.find(link.target.modId);
    if (teIt == scopeTeMods.end() || !teIt->second)
        return nullptr;

    bool sync = false;
    for (const auto& m : scopeMods) {
        if (m.id == link.target.modId) {
            sync = m.tempoSync;
            break;
        }
    }
    const juce::String wantedID =
        link.target.modParamIndex == 0 ? (sync ? "rateType" : "rate") : "depth";
    for (auto* p : teIt->second->getAutomatableParameters()) {
        if (p && p->paramID == wantedID)
            return p;
    }
    return nullptr;
}

ChainNodePath parentContainerForMoveSource(const ChainNodePath& sourceElementPath) {
    ChainNodePath parent;
    parent.trackId = sourceElementPath.trackId;

    if (sourceElementPath.topLevelDeviceId != INVALID_DEVICE_ID)
        return parent;

    parent.steps = sourceElementPath.steps;
    if (!parent.steps.empty())
        parent.steps.pop_back();
    return parent;
}

void collectDevicePathsForMove(const RackInfo& rack, const ChainNodePath& rackPath,
                               std::vector<ChainNodePath>& out);

void collectDevicePathsForMove(const ChainElement& element, const ChainNodePath& parentChainPath,
                               std::vector<ChainNodePath>& out) {
    if (isDevice(element)) {
        out.push_back(parentChainPath.withDevice(getDevice(element).id));
    } else if (isRack(element)) {
        const auto& rack = getRack(element);
        collectDevicePathsForMove(rack, parentChainPath.withRack(rack.id), out);
    }
}

void collectDevicePathsForMove(const RackInfo& rack, const ChainNodePath& rackPath,
                               std::vector<ChainNodePath>& out) {
    for (const auto& chain : rack.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        for (const auto& child : chain.elements)
            collectDevicePathsForMove(child, chainPath, out);
    }
}

void collectValidDevicePaths(const std::vector<ChainElement>& elements,
                             const ChainNodePath& parentPath,
                             std::set<ChainNodePath>& validDevicePaths,
                             std::set<RackId>& validRackIds) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            validDevicePaths.insert(parentPath.withDevice(getDevice(element).id));
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            validRackIds.insert(rack.id);
            const auto rackPath = parentPath.withRack(rack.id);
            for (const auto& chain : rack.chains)
                collectValidDevicePaths(chain.elements, rackPath.withChain(chain.id),
                                        validDevicePaths, validRackIds);
        }
    }
}

void collectValidDevicePaths(const TrackInfo& track, std::set<ChainNodePath>& validDevicePaths,
                             std::set<RackId>& validRackIds) {
    for (const auto& element : track.chain.fxChainElements) {
        if (isDevice(element)) {
            validDevicePaths.insert(ChainNodePath::topLevelDevice(track.id, getDevice(element).id));
        } else if (isRack(element)) {
            const auto& rack = getRack(element);
            validRackIds.insert(rack.id);
            const auto rackPath = ChainNodePath::rack(track.id, rack.id);
            for (const auto& chain : rack.chains)
                collectValidDevicePaths(chain.elements, rackPath.withChain(chain.id),
                                        validDevicePaths, validRackIds);
        }
    }

    for (const auto& elem : track.chain.postFxChainElements)
        validDevicePaths.insert(ChainNodePath::postFxDevice(track.id, elem.device.id));
    for (const auto& elem : track.chain.mixerAnalysisElements)
        validDevicePaths.insert(ChainNodePath::mixerAnalysisDevice(track.id, elem.device.id));
}

// Capture the VST3 .vstpreset for a device: the class id (stable, cached once)
// and the current preset blob (refreshed each capture, since the patch changes).
// One getPreset() call yields both - the class id lives in the preset header.
// These feed the portable DAWproject deviceID + <State>.
void captureVst3Info(DeviceInfo& devInfo, te::ExternalPlugin* ext) {
    if (ext == nullptr)
        return;
    auto* pi = ext->getAudioPluginInstance();
    if (pi == nullptr)
        return;

    struct PresetVisitor : juce::ExtensionsVisitor {
        juce::MemoryBlock data;
        void visitVST3Client(const VST3Client& client) override {
            data = client.getPreset();
        }
    };
    PresetVisitor visitor;
    pi->getExtensions(visitor);

    const auto& preset = visitor.data;
    if (preset.getSize() == 0)
        return;  // not a VST3 / no preset
    if (devInfo.vst3ClassId.isEmpty())
        devInfo.vst3ClassId = vst3::classIdFromPreset(preset);
    devInfo.vst3Preset = juce::Base64::toBase64(preset.getData(), preset.getSize());
}

}  // namespace

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

PluginManager::SyncedDeviceMap::iterator PluginManager::findSyncedDevice(
    const ChainNodePath& devicePath) {
    return syncedDevices_.find(devicePath);
}

PluginManager::SyncedDeviceMap::const_iterator PluginManager::findSyncedDevice(
    const ChainNodePath& devicePath) const {
    return syncedDevices_.find(devicePath);
}

void PluginManager::prepareForChainElementMove(const ChainNodePath& sourceElementPath,
                                               const ChainNodePath& destinationChainPath) {
    captureAllPluginStates();

    const auto sourceContainerPath = parentContainerForMoveSource(sourceElementPath);
    if (sourceContainerPath == destinationChainPath) {
        return;
    }

    auto& trackManager = TrackManager::getInstance();
    std::vector<ChainNodePath> devicePaths;

    if (sourceElementPath.topLevelDeviceId != INVALID_DEVICE_ID) {
        devicePaths.push_back(sourceElementPath);
    } else if (!sourceElementPath.steps.empty() &&
               sourceElementPath.steps.back().type == ChainStepType::Device) {
        devicePaths.push_back(sourceElementPath);
    } else if (!sourceElementPath.steps.empty() &&
               sourceElementPath.steps.back().type == ChainStepType::Rack) {
        if (auto* rack = trackManager.getRackByPath(sourceElementPath)) {
            collectDevicePathsForMove(*rack, sourceElementPath, devicePaths);
        }
    }

    for (const auto& devicePath : devicePaths)
        detachDeviceRuntimeForChainMove(devicePath);

    if (!sourceElementPath.steps.empty() &&
        sourceElementPath.steps.back().type == ChainStepType::Rack)
        detachRackRuntimeForChainMove(sourceElementPath.steps.back().id);
}

// =============================================================================
// Plugin/Device Lookup
// =============================================================================

te::Plugin::Ptr PluginManager::getPlugin(const ChainNodePath& devicePath) const {
    juce::ScopedLock lock(pluginLock_);
    auto it = findSyncedDevice(devicePath);
    if (it != syncedDevices_.end() && it->second.plugin)
        return it->second.plugin;

    // RackSyncManager is still the fallback for rack internals that have not
    // been mirrored into syncedDevices_ yet. Flat track sections are keyed by
    // full ChainNodePath; falling back by bare DeviceId can alias master,
    // post-FX, mixer-analysis, or sibling multi-out devices.
    if (devicePath.topLevelDeviceId == INVALID_DEVICE_ID && !devicePath.isPostFx() &&
        !devicePath.isMixerAnalysis()) {
        if (auto* innerPlugin = rackSyncManager_.getInnerPlugin(devicePath.getDeviceId()))
            return innerPlugin;
    }

    return nullptr;
}

DeviceProcessor* PluginManager::getDeviceProcessor(const ChainNodePath& devicePath) const {
    juce::ScopedLock lock(pluginLock_);
    auto it = findSyncedDevice(devicePath);
    return it != syncedDevices_.end() ? it->second.processor.get() : nullptr;
}

DeviceId PluginManager::getDeviceIdForPlugin(te::Plugin* plugin) const {
    if (!plugin)
        return INVALID_DEVICE_ID;

    juce::ScopedLock lock(pluginLock_);
    auto it = pluginToDevice_.find(plugin);
    if (it != pluginToDevice_.end())
        return it->second.getDeviceId();

    // Instrument wrapper rack instances deliberately do not resolve here. Their
    // visible MAGDA metering/gain is handled by InstrumentMeterTapPlugin inside
    // the rack, so the TE graph hook must not meter the whole rack output and
    // include upstream audio passthrough.
    if (instrumentRackManager_.isWrapperRack(plugin))
        return INVALID_DEVICE_ID;

    return INVALID_DEVICE_ID;
}

ChainNodePath PluginManager::getDevicePathForPlugin(te::Plugin* plugin) const {
    if (!plugin)
        return {};

    juce::ScopedLock lock(pluginLock_);
    auto it = pluginToDevice_.find(plugin);
    if (it != pluginToDevice_.end())
        return it->second;

    return {};
}

// =============================================================================
// MIDI Receive Plugin Lifecycle
// =============================================================================

void PluginManager::ensureMidiReceive(const ChainNodePath& devicePath, TrackId sourceTrackId) {
    const auto trackId = devicePath.trackId;
    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (!teTrack)
        return;

    auto it = findSyncedDevice(devicePath);
    if (it == syncedDevices_.end())
        return;

    // Find the target device's TE plugin to insert before it
    auto targetPlugin = getPlugin(devicePath);
    int insertPos = -1;
    if (targetPlugin) {
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            if (teTrack->pluginList[i] == targetPlugin.get()) {
                insertPos = i;
                break;
            }
        }
    }

    auto makeMidiReceivePlugin = [&](TrackId midiSourceTrackId,
                                     bool replaceExistingMidi) -> te::Plugin::Ptr {
        juce::ValueTree pluginState(te::IDs::PLUGIN);
        pluginState.setProperty(te::IDs::type, MidiReceivePlugin::xmlTypeName, nullptr);
        pluginState.setProperty(juce::Identifier("sourceTrackId"), midiSourceTrackId, nullptr);
        pluginState.setProperty(juce::Identifier("replaceExistingMidi"), replaceExistingMidi,
                                nullptr);

        auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
        if (auto* rx = dynamic_cast<MidiReceivePlugin*>(plugin.get())) {
            rx->setSourceTrackId(midiSourceTrackId);
            rx->setReplaceExistingMidi(replaceExistingMidi);
        }
        return plugin;
    };

    auto configureSidechainDependency = [&](te::Plugin::Ptr plugin, TrackId midiSourceTrackId) {
        if (!plugin || midiSourceTrackId == trackId)
            return;

        // Set sidechain source to create a graph dependency on the source track.
        // This ensures TE processes the source track (with SidechainMonitorPlugin)
        // before this plugin, so MidiBroadcastBus contains current-block MIDI.
        auto* sourceTeTrack = trackController_.getAudioTrack(midiSourceTrackId);
        if (sourceTeTrack) {
            plugin->setSidechainSourceID(sourceTeTrack->itemID);
            plugin->guessSidechainRouting();
        }
    };

    if (!it->second.midiReceivePlugin) {
        it->second.midiReceivePlugin = makeMidiReceivePlugin(sourceTrackId, true);
        if (it->second.midiReceivePlugin)
            teTrack->pluginList.insertPlugin(it->second.midiReceivePlugin, insertPos, nullptr);
    } else if (auto* rx = dynamic_cast<MidiReceivePlugin*>(it->second.midiReceivePlugin.get())) {
        rx->setSourceTrackId(sourceTrackId);
        rx->setReplaceExistingMidi(true);
    }

    configureSidechainDependency(it->second.midiReceivePlugin, sourceTrackId);

    if (targetPlugin) {
        const int targetPos = teTrack->pluginList.indexOf(targetPlugin.get());
        const int currentReceivePos =
            teTrack->pluginList.indexOf(it->second.midiReceivePlugin.get());
        if (currentReceivePos >= 0 && targetPos >= 0 && currentReceivePos != targetPos - 1) {
            auto& listState = teTrack->pluginList.state;
            const int receiveChild = listState.indexOf(it->second.midiReceivePlugin->state);
            const int targetChild = listState.indexOf(targetPlugin->state);
            if (receiveChild >= 0 && targetChild >= 0) {
                listState.moveChild(receiveChild, targetChild, nullptr);
                requestPluginOrderGraphRestart(trackId, "midi-sidechain-receive");
            }
        }
    }

    if (!it->second.midiRestorePlugin) {
        it->second.midiRestorePlugin = makeMidiReceivePlugin(trackId, true);
        if (it->second.midiRestorePlugin) {
            const int targetPos =
                targetPlugin ? teTrack->pluginList.indexOf(targetPlugin.get()) : -1;
            teTrack->pluginList.insertPlugin(it->second.midiRestorePlugin,
                                             targetPos >= 0 ? targetPos + 1 : -1, nullptr);
        }
    } else if (auto* rx = dynamic_cast<MidiReceivePlugin*>(it->second.midiRestorePlugin.get())) {
        rx->setSourceTrackId(trackId);
        rx->setReplaceExistingMidi(true);
    }

    if (targetPlugin && it->second.midiRestorePlugin) {
        const int targetPos = teTrack->pluginList.indexOf(targetPlugin.get());
        const int currentRestorePos =
            teTrack->pluginList.indexOf(it->second.midiRestorePlugin.get());
        if (currentRestorePos >= 0 && targetPos >= 0 && currentRestorePos != targetPos + 1) {
            auto& listState = teTrack->pluginList.state;
            const int restoreChild = listState.indexOf(it->second.midiRestorePlugin->state);
            const int targetChild = listState.indexOf(targetPlugin->state);
            if (restoreChild >= 0 && targetChild >= 0) {
                listState.moveChild(restoreChild, targetChild + 1, nullptr);
                requestPluginOrderGraphRestart(trackId, "midi-sidechain-restore");
            }
        }
    }

    it->second.trackId = trackId;
}

void PluginManager::removeMidiReceive(const ChainNodePath& devicePath) {
    auto it = findSyncedDevice(devicePath);
    if (it == syncedDevices_.end())
        return;

    auto* plugin = it->second.midiReceivePlugin.get();
    auto* restorePlugin = it->second.midiRestorePlugin.get();
    it->second.midiReceivePlugin = nullptr;
    it->second.midiRestorePlugin = nullptr;

    if (plugin)
        plugin->deleteFromParent();
    if (restorePlugin)
        restorePlugin->deleteFromParent();
}

void PluginManager::requestPluginOrderGraphRestart(TrackId trackId, const juce::String& reason) {
    if (onPluginOrderGraphRestartRequested)
        onPluginOrderGraphRestartRequested(trackId, reason);
    edit_.restartPlayback();
}

// =============================================================================
// Plugin State Capture/Restore
// =============================================================================

void PluginManager::captureAllPluginStates() {
    {
        juce::ScopedLock lock(pluginLock_);

        for (const auto& [devicePath, sd] : syncedDevices_) {
            if (!sd.plugin)
                continue;

            juce::String stateStr;

            te::ExternalPlugin* capturedExt = nullptr;
            if (auto* ext = dynamic_cast<te::ExternalPlugin*>(sd.plugin.get())) {
                // External plugin: capture base64 blob from TE state property
                ext->flushPluginStateToValueTree();
                stateStr = ext->state.getProperty(te::IDs::state).toString();
                capturedExt = ext;
            } else {
                // TE internal plugin (4osc, EQ, Compressor, etc.):
                // Capture the full ValueTree as XML so non-automatable
                // CachedValues (wave shapes, filter type, etc.) are preserved.
                // Strip TE ids recursively so duplicated tracks get fresh
                // EditItemIDs all the way through nested state trees.
                sd.plugin->flushPluginStateToValueTree();
                auto stateCopy = sd.plugin->state.createCopy();
                stripTracktionIdsRecursive(stateCopy);
                stripModifierAssignmentsRecursive(stateCopy);
                if (auto xml = stateCopy.createXml())
                    stateStr = xml->toString();
            }

            // Always overwrite pluginState (even if empty) to avoid stale state.
            auto& trackManager = TrackManager::getInstance();
            auto* devInfo = trackManager.getDeviceInChainByPath(devicePath);
            if (devInfo) {
                devInfo->pluginState = stateStr;
                captureVst3Info(*devInfo, capturedExt);
                if (sd.processor != nullptr)
                    sd.processor->populateParameters(*devInfo);
            }
        }
    }

    // Also capture state from plugins inside racks
    rackSyncManager_.captureAllPluginStates();
}

void PluginManager::capturePluginState(const ChainNodePath& devicePath) {
    juce::ScopedLock lock(pluginLock_);

    auto it = findSyncedDevice(devicePath);
    if (it == syncedDevices_.end() || !it->second.plugin) {
        return;
    }

    auto* plugin = it->second.plugin.get();
    juce::String stateStr;

    te::ExternalPlugin* capturedExt = nullptr;
    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin)) {
        ext->flushPluginStateToValueTree();
        stateStr = ext->state.getProperty(te::IDs::state).toString();
        capturedExt = ext;
    } else {
        plugin->flushPluginStateToValueTree();
        auto stateCopy = plugin->state.createCopy();
        stripTracktionIdsRecursive(stateCopy);
        stripModifierAssignmentsRecursive(stateCopy);
        if (auto xml = stateCopy.createXml())
            stateStr = xml->toString();
    }

    auto& trackManager = TrackManager::getInstance();
    if (auto* devInfo = trackManager.getDeviceInChainByPath(devicePath)) {
        devInfo->pluginState = stateStr;
        captureVst3Info(*devInfo, capturedExt);
        if (it->second.processor)
            it->second.processor->populateParameters(*devInfo);
    }
}

void PluginManager::removeDrumGridPadDevicesLocked(const ChainNodePath& drumGridPath) {
    auto padIt = drumGridPadDevices_.find(drumGridPath);
    if (padIt == drumGridPadDevices_.end())
        return;

    for (const auto& padPath : padIt->second) {
        auto padSdIt = findSyncedDevice(padPath);
        if (padSdIt != syncedDevices_.end()) {
            if (padSdIt->second.plugin)
                pluginToDevice_.erase(padSdIt->second.plugin.get());
            syncedDevices_.erase(padSdIt);
        }
    }
    drumGridPadDevices_.erase(padIt);
}

void PluginManager::detachDeviceRuntimeForChainMove(const ChainNodePath& devicePath) {
    const auto deviceId = devicePath.getDeviceId();
    te::Plugin::Ptr plugin;
    te::Plugin::Ptr midiReceivePlugin;
    bool wasMapped = false;

    {
        juce::ScopedLock lock(pluginLock_);
        auto it = findSyncedDevice(devicePath);
        if (it == syncedDevices_.end())
            return;

        wasMapped = true;
        plugin = it->second.plugin;
        midiReceivePlugin = it->second.midiReceivePlugin;

        clearLFOCustomWaveCallbacks(it->second.modifiers);
        deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);

        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get())) {
            dg->removeListener(this);
            removeDrumGridPadDevicesLocked(devicePath);
        }

        if (it->second.plugin)
            pluginToDevice_.erase(it->second.plugin.get());
        if (it->second.midiReceivePlugin)
            pluginToDevice_.erase(it->second.midiReceivePlugin.get());

        syncedDevices_.erase(it);
    }

    if (!wasMapped)
        return;

    pluginWindowBridge_.closeWindowsForDevice(deviceId);

    if (midiReceivePlugin)
        midiReceivePlugin->deleteFromParent();

    if (devicePath.getType() == ChainNodeType::TopLevelDevice &&
        instrumentRackManager_.getInnerPlugin(deviceId) != nullptr) {
        instrumentRackManager_.unwrap(deviceId);
    } else if (plugin && plugin->getOwnerTrack() != nullptr) {
        plugin->deleteFromParent();
    }
}

void PluginManager::detachRackRuntimeForChainMove(RackId rackId) {
    rackSyncManager_.removeRackForMove(rackId);
}

void PluginManager::restorePluginState(const ChainNodePath& devicePath, te::Plugin::Ptr plugin) {
    auto& tm = TrackManager::getInstance();
    auto* devInfo = tm.getDeviceInChainByPath(devicePath);
    if (!devInfo || devInfo->pluginState.isEmpty()) {
        return;
    }

    if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin.get())) {
        ext->state.setProperty(te::IDs::state, devInfo->pluginState, nullptr);
    } else {
        // Internal plugin: restore from saved XML ValueTree
        if (auto xml = juce::parseXML(devInfo->pluginState)) {
            auto savedState = juce::ValueTree::fromXml(*xml);
            if (savedState.isValid()) {
                plugin->restorePluginStateFromValueTree(savedState);
            }
        }
    }
}

void PluginManager::purgeStaleEntries() {
    auto& tm = TrackManager::getInstance();

    // Collect all valid device paths from TrackManager (including rack inner
    // devices and flat post-fx / mixer-analysis sections).
    std::set<ChainNodePath> validDevicePaths;
    std::set<TrackId> validTrackIds;
    std::set<RackId> validRackIds;

    for (const auto& track : tm.getTracks()) {
        validTrackIds.insert(track.id);
        collectValidDevicePaths(track, validDevicePaths, validRackIds);
    }

    if (auto* masterTrack = tm.getTrack(MASTER_TRACK_ID)) {
        validTrackIds.insert(MASTER_TRACK_ID);
        collectValidDevicePaths(*masterTrack, validDevicePaths, validRackIds);
    }

    // Purge stale entries from maps
    int purged = 0;
    std::vector<te::Plugin::Ptr> pluginsToDelete;
    std::vector<te::Plugin*> midiPluginsToDelete;
    std::vector<te::Plugin*> monitorPluginsToDelete;
    {
        juce::ScopedLock lock(pluginLock_);

        // syncedDevices_ (consolidates all per-device maps)
        deferredHolders_.clear();  // Drain previous cycle's deferred holders
        for (auto it = syncedDevices_.begin(); it != syncedDevices_.end();) {
            if (validDevicePaths.find(it->first) == validDevicePaths.end()) {
                // Clear LFO callbacks before destroying CurveSnapshotHolders
                clearLFOCustomWaveCallbacks(it->second.modifiers);
                deferCurveSnapshots(it->second.curveSnapshots, deferredHolders_);
                if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(it->second.plugin.get()))
                    dg->removeListener(this);
                if (it->second.plugin)
                    pluginToDevice_.erase(it->second.plugin.get());
                if (it->second.midiReceivePlugin)
                    midiPluginsToDelete.push_back(it->second.midiReceivePlugin.get());
                if (it->second.midiRestorePlugin)
                    midiPluginsToDelete.push_back(it->second.midiRestorePlugin.get());
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
                    monitorPluginsToDelete.push_back(it->second.get());
                it = sidechainMonitors_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Delete plugins outside the lock to avoid blocking and re-entrancy
    for (auto* plugin : midiPluginsToDelete) {
        if (plugin)
            plugin->deleteFromParent();
    }
    for (auto& plugin : pluginsToDelete) {
        plugin->deleteFromParent();
    }
    for (auto* plugin : monitorPluginsToDelete) {
        if (plugin)
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
        rebuildSidechainLFOCache();
    }
}

void PluginManager::validateMappingConsistency() {
#if JUCE_DEBUG
    juce::ScopedLock lock(pluginLock_);

    // 1. Every syncedDevices_ entry's plugin owner track should exist in TrackController
    for (const auto& [devicePath, sd] : syncedDevices_) {
        if (!sd.plugin) {
            continue;  // Some entries may only have processor (rack inner plugins)
        }
        auto* owner = sd.plugin->getOwnerTrack();
        if (owner) {
            if (devicePath.trackId == MASTER_TRACK_ID) {
                const auto& masterList = edit_.getMasterPluginList();
                bool foundOnMaster = false;
                for (int i = 0; i < masterList.size(); ++i) {
                    if (masterList[i] == sd.plugin.get()) {
                        foundOnMaster = true;
                        break;
                    }
                }
                if (foundOnMaster)
                    continue;
            }

            bool found = false;
            for (auto trackId : trackController_.getAllTrackIds()) {
                if (trackController_.getAudioTrack(trackId) == owner) {
                    found = true;
                    break;
                }
            }
            if (!found) {
            }
        }
    }

    // 2. (removed — struct guarantees modifier ↔ plugin consistency)

    // 3. Every sidechainMonitors_ TrackId should exist in TrackController
    for (const auto& [trackId, plugin] : sidechainMonitors_) {
        if (trackController_.getAudioTrack(trackId) == nullptr) {
        }
    }

    // 4. Every synced rack's trackId should exist in TrackController
    auto syncedRackIds = rackSyncManager_.getSyncedRackIds();
    for (auto rackId : syncedRackIds) {
        // Can't easily check trackId without exposing internals, but we can check
        // the rack exists in TrackManager
        bool found = false;
        for (const auto& track : TrackManager::getInstance().getTracks()) {
            for (const auto& element : track.chain.fxChainElements) {
                if (isRack(element) && getRack(element).id == rackId) {
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found) {
        }
    }
#endif
}

void PluginManager::clearAllMappings() {
    juce::ScopedLock lock(pluginLock_);
    // Clear LFO callbacks and defer holder destruction
    for (auto& [devicePath, sd] : syncedDevices_) {
        clearLFOCustomWaveCallbacks(sd.modifiers);
        deferCurveSnapshots(sd.curveSnapshots, deferredHolders_);
        // Unregister DrumGrid listener to avoid dangling pointer
        if (auto* dg = dynamic_cast<daw::audio::DrumGridPlugin*>(sd.plugin.get()))
            dg->removeListener(this);
        else if (auto* inner = instrumentRackManager_.getInnerPlugin(devicePath.getDeviceId()))
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
    if (renderingActive_.load(std::memory_order_relaxed))
        return;

    for (const auto& [deviceId, sd] : syncedDevices_) {
        if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(sd.processor.get())) {
            // Test Tone is always transport-synced
            // Simply bypass when stopped, enable when playing
            toneProc->setBypassed(!isPlaying);
        }
    }
}

}  // namespace magda
