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
    //
    // Must recurse into rack chains: an LFO on a 4OSC sitting inside a rack
    // chain still listens on this track's MIDI bus for retrigger, so missing
    // it here would leave the SidechainMonitor uninstalled and self-trigger
    // LFOs silently dead in that scope.
    std::function<bool(const std::vector<ChainElement>&)> hasMidiTriggeredMod =
        [&](const std::vector<ChainElement>& elements) -> bool {
        for (const auto& element : elements) {
            if (isDevice(element)) {
                for (const auto& mod : getDevice(element).mods)
                    if (mod.triggerMode == LFOTriggerMode::MIDI)
                        return true;
            } else if (isRack(element)) {
                const auto& rack = getRack(element);
                for (const auto& mod : rack.mods)
                    if (mod.triggerMode == LFOTriggerMode::MIDI)
                        return true;
                for (const auto& chain : rack.chains)
                    if (hasMidiTriggeredMod(chain.elements))
                        return true;
            }
        }
        return false;
    };
    if (hasMidiTriggeredMod(trackInfo->chainElements))
        return true;

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
// Audio Sidechain Monitor Plugin Lifecycle
// =============================================================================

bool PluginManager::trackNeedsAudioSidechainMonitor(TrackId trackId) const {
    // Check if any device sidechained from this track has an Audio-triggered mod.
    // The sidechain routing type (MIDI vs Audio) is independent of the LFO trigger
    // mode, so we check the mod's triggerMode rather than sidechain.type.
    auto deviceHasAudioTrigger = [&](const DeviceInfo& device) {
        if (device.sidechain.sourceTrackId != trackId)
            return false;
        for (const auto& mod : device.mods) {
            if (mod.triggerMode == LFOTriggerMode::Audio)
                return true;
        }
        return false;
    };

    for (const auto& track : TrackManager::getInstance().getTracks()) {
        for (const auto& element : track.chainElements) {
            if (isDevice(element)) {
                if (deviceHasAudioTrigger(getDevice(element)))
                    return true;
            } else if (isRack(element)) {
                const auto& rack = getRack(element);
                // Check rack-level mods
                if (rack.sidechain.sourceTrackId == trackId) {
                    for (const auto& mod : rack.mods) {
                        if (mod.triggerMode == LFOTriggerMode::Audio)
                            return true;
                    }
                }
                // Check devices inside rack
                for (const auto& chain : rack.chains) {
                    for (const auto& ce : chain.elements) {
                        if (isDevice(ce) && deviceHasAudioTrigger(getDevice(ce)))
                            return true;
                    }
                }
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
    auto plugin = it->second;
    audioSidechainMonitors_.erase(it);

    if (plugin)
        plugin->deleteFromParent();
}

}  // namespace magda
