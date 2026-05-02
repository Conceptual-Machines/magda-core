#include "PluginManager.hpp"

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
            // Strip TE ids recursively so duplicated tracks get fresh
            // EditItemIDs all the way through nested state trees.
            sd.plugin->flushPluginStateToValueTree();
            auto stateCopy = sd.plugin->state.createCopy();
            stripTracktionIdsRecursive(stateCopy);
            if (auto xml = stateCopy.createXml())
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
        auto stateCopy = plugin->state.createCopy();
        stripTracktionIdsRecursive(stateCopy);
        if (auto xml = stateCopy.createXml())
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
