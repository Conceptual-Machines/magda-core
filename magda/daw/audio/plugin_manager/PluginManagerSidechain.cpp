#include <set>
#include <unordered_set>
#include <vector>

#include "../../core/ChainRoutingModel.hpp"
#include "../../core/RackInfo.hpp"
#include "../../core/SidechainTraversal.hpp"
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
// Sidechain Routing Sync
// =============================================================================

void PluginManager::syncSidechains(TrackId trackId, te::AudioTrack* teTrack) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    const auto topLevelRoutingPlan =
        routing::compileTrackChainRouting(trackId, trackInfo->chain.fxChainElements);
    auto findTopLevelRoutingNode = [&](DeviceId deviceId) -> const routing::ChainRoutingNode* {
        for (const auto& node : topLevelRoutingPlan.nodes) {
            if (node.kind == routing::ChainRoutingNodeKind::Device && node.deviceId == deviceId)
                return &node;
        }
        return nullptr;
    };

    auto syncTopLevelDevice = [&](const DeviceInfo& device) {
        const auto devicePath = ChainNodePath::topLevelDevice(trackId, device.id);
        auto plugin = getPlugin(devicePath);

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
        if (const auto* route = findTopLevelRoutingNode(device.id);
            route != nullptr && route->usesExternalMidiSidechain()) {
            ensureMidiReceive(devicePath, route->midiSidechainSourceTrackId);
        } else {
            removeMidiReceive(devicePath);
        }
    };

    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (isDevice(element)) {
            syncTopLevelDevice(getDevice(element));
        } else if (isRack(element)) {
            rackSyncManager_.syncSidechains(getRack(element), [this](TrackId sourceTrackId) {
                return trackController_.getAudioTrack(sourceTrackId);
            });
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
    //
    // Must recurse into rack chains: an LFO on a device sitting inside a rack
    // chain still listens on this track's MIDI bus for retrigger.
    if (sidechain::elementsHaveMidiTriggeredMod(trackInfo->chain.fxChainElements))
        return true;

    const auto topLevelRoutingPlan =
        routing::compileTrackChainRouting(trackId, trackInfo->chain.fxChainElements);
    for (const auto& node : topLevelRoutingPlan.nodes) {
        if (node.kind == routing::ChainRoutingNodeKind::Device &&
            node.usesExternalMidiSidechain()) {
            return true;
        }
    }

    // Check if this track is a MIDI sidechain source for any other track
    for (const auto& track : TrackManager::getInstance().getTracks()) {
        if (sidechain::elementsUseSource(track.chain.fxChainElements, trackId,
                                         SidechainConfig::Type::MIDI))
            return true;
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
    auto* plugin = it->second.get();
    sidechainMonitors_.erase(it);

    if (plugin)
        plugin->deleteFromParent();
}

// =============================================================================
// Audio Sidechain Monitor Plugin Lifecycle
// =============================================================================

bool PluginManager::trackNeedsAudioSidechainMonitor(TrackId trackId) const {
    // Check if any device sidechained from this track has an Audio-triggered mod.
    // The sidechain routing type (MIDI vs Audio) is independent of the LFO trigger
    // mode, so we check the mod's triggerMode rather than sidechain.type.
    auto deviceHasAudioTrigger = [&](const DeviceInfo& device) {
        if (!sidechain::deviceUsesSource(device, trackId))
            return false;
        for (const auto& mod : device.mods) {
            if (mod.triggerMode == LFOTriggerMode::Audio)
                return true;
        }
        return false;
    };

    for (const auto& track : TrackManager::getInstance().getTracks()) {
        for (const auto& element : track.chain.fxChainElements) {
            if (isDevice(element)) {
                if (deviceHasAudioTrigger(getDevice(element)))
                    return true;
            } else if (isRack(element)) {
                if (sidechain::rackHasAudioTriggeredModForSource(getRack(element), trackId))
                    return true;
            }
        }
    }
    return false;
}

void PluginManager::checkAudioSidechainMonitor(TrackId trackId) {
    if (trackNeedsAudioSidechainMonitor(trackId))
        ensureAudioSidechainMonitor(trackId);
    else
        removeAudioSidechainMonitor(trackId);

    rebuildSidechainLFOCache();
}

void PluginManager::ensureAudioSidechainMonitor(TrackId sourceTrackId) {
    if (audioSidechainMonitors_.count(sourceTrackId) > 0)
        return;

    auto* teTrack = trackController_.getAudioTrack(sourceTrackId);
    if (!teTrack) {
        DBG("PluginManager::ensureAudioSidechainMonitor - track " << sourceTrackId
                                                                  << " has no TE AudioTrack");
        return;
    }

    // Check if an AudioSidechainMonitorPlugin already exists on the track
    for (int i = 0; i < teTrack->pluginList.size(); ++i) {
        if (dynamic_cast<AudioSidechainMonitorPlugin*>(teTrack->pluginList[i])) {
            DBG("PluginManager::ensureAudioSidechainMonitor - track "
                << sourceTrackId << " found existing audio monitor plugin on TE track");
            audioSidechainMonitors_[sourceTrackId] = teTrack->pluginList[i];
            auto* mon = dynamic_cast<AudioSidechainMonitorPlugin*>(teTrack->pluginList[i]);
            mon->setSourceTrackId(sourceTrackId);
            mon->setPluginManager(this);
            return;
        }
    }

    // Create a new audio monitor plugin
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, AudioSidechainMonitorPlugin::xmlTypeName, nullptr);
    pluginState.setProperty(juce::Identifier("sourceTrackId"), sourceTrackId, nullptr);

    DBG("PluginManager::ensureAudioSidechainMonitor - creating new audio monitor for track "
        << sourceTrackId);
    auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    if (plugin) {
        if (auto* mon = dynamic_cast<AudioSidechainMonitorPlugin*>(plugin.get())) {
            mon->setSourceTrackId(sourceTrackId);
            mon->setPluginManager(this);
        }
        // Insert near the end of the chain so it sees generated audio.
        // TE's LevelMeterPlugin and VolumeAndPanPlugin are auto-added at the very end,
        // so inserting at the last position before those is ideal.
        int insertPos = teTrack->pluginList.size();
        // Walk backwards past TE's built-in tail plugins
        for (int i = teTrack->pluginList.size() - 1; i >= 0; --i) {
            auto* p = teTrack->pluginList[i];
            if (dynamic_cast<te::LevelMeterPlugin*>(p) ||
                dynamic_cast<te::VolumeAndPanPlugin*>(p)) {
                insertPos = i;
            } else {
                break;
            }
        }
        teTrack->pluginList.insertPlugin(plugin, insertPos, nullptr);
        audioSidechainMonitors_[sourceTrackId] = plugin;
        DBG("PluginManager::ensureAudioSidechainMonitor - inserted audio monitor at position "
            << insertPos << " on track " << sourceTrackId);
    } else {
        DBG("PluginManager::ensureAudioSidechainMonitor - FAILED to create audio monitor for track "
            << sourceTrackId);
    }
}

void PluginManager::removeAudioSidechainMonitor(TrackId sourceTrackId) {
    auto it = audioSidechainMonitors_.find(sourceTrackId);
    if (it == audioSidechainMonitors_.end())
        return;

    DBG("PluginManager::removeAudioSidechainMonitor - removing audio monitor from track "
        << sourceTrackId);
    auto* plugin = it->second.get();
    audioSidechainMonitors_.erase(it);

    if (plugin)
        plugin->deleteFromParent();
}

}  // namespace magda
