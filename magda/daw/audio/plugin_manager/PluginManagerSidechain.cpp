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
#include "modifiers/ADSRDebugLog.hpp"
#include "modifiers/CurveSnapshot.hpp"
#include "modifiers/ModifierHelpers.hpp"
#include "modifiers/ModifierSync.hpp"
#include "plugins/ArpeggiatorPlugin.hpp"
#include "plugins/AudioSidechainMonitorPlugin.hpp"
#include "plugins/DrumGridPlugin.hpp"
#include "plugins/FollowerSourceTapPlugin.hpp"
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

namespace {

bool rackContainsInstrumentSource(const RackInfo& rack) {
    for (const auto& chain : rack.chains) {
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                if (getDevice(element).isInstrument)
                    return true;
            } else if (isRack(element) && rackContainsInstrumentSource(getRack(element))) {
                return true;
            }
        }
    }

    return false;
}

}  // namespace

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
    if (trackId < 0 || trackId >= kMaxCacheTracks)
        return false;

    auto* cache = activeCache_.load(std::memory_order_acquire);
    return cache && cache->entries[static_cast<size_t>(trackId)].hasAudioTrigger;
}

void PluginManager::checkAudioSidechainMonitor(TrackId trackId) {
    rebuildSidechainLFOCache();

    const bool needed = trackNeedsAudioSidechainMonitor(trackId);
    MAGDA_ADSR_AUDIO_LOG("check monitor sourceTrack="
                         << trackId << " needed=" << static_cast<int>(needed) << " exists="
                         << static_cast<int>(audioSidechainMonitors_.count(trackId) > 0));

    if (needed)
        ensureAudioSidechainMonitor(trackId);
    else
        removeAudioSidechainMonitor(trackId);

    if (trackNeedsFollowerSourceTap(trackId))
        ensureFollowerSourceTap(trackId);
    else
        removeFollowerSourceTap(trackId);
}

void PluginManager::refreshAudioSidechainMonitors() {
    rebuildSidechainLFOCache();

    for (const auto& track : TrackManager::getInstance().getTracks()) {
        const bool needed = trackNeedsAudioSidechainMonitor(track.id);
        MAGDA_ADSR_AUDIO_LOG("refresh monitor sourceTrack="
                             << track.id << " needed=" << static_cast<int>(needed) << " exists="
                             << static_cast<int>(audioSidechainMonitors_.count(track.id) > 0));

        if (needed)
            ensureAudioSidechainMonitor(track.id);
        else
            removeAudioSidechainMonitor(track.id);

        if (trackNeedsFollowerSourceTap(track.id))
            ensureFollowerSourceTap(track.id);
        else
            removeFollowerSourceTap(track.id);
    }
}

void PluginManager::ensureAudioSidechainMonitor(TrackId sourceTrackId) {
    auto* teTrack = trackController_.getAudioTrack(sourceTrackId);
    if (!teTrack) {
        DBG("PluginManager::ensureAudioSidechainMonitor - track " << sourceTrackId
                                                                  << " has no TE AudioTrack");
        MAGDA_ADSR_AUDIO_LOG(
            "cannot create monitor; missing TE track sourceTrack=" << sourceTrackId);
        return;
    }

    auto computeInsertPos = [&]() {
        auto frontInsertPos = [&]() {
            int pos = 0;
            for (int i = 0; i < teTrack->pluginList.size(); ++i) {
                auto* plugin = teTrack->pluginList[i];
                if (dynamic_cast<te::AuxReturnPlugin*>(plugin) ||
                    dynamic_cast<SidechainMonitorPlugin*>(plugin)) {
                    pos = i + 1;
                    continue;
                }
                break;
            }
            return pos;
        };

        auto pluginIndex = [&](te::Plugin* plugin) {
            return plugin ? teTrack->pluginList.indexOf(plugin) : -1;
        };

        if (auto* trackInfo = TrackManager::getInstance().getTrack(sourceTrackId)) {
            for (const auto& element : trackInfo->chain.fxChainElements) {
                if (isDevice(element)) {
                    const auto& device = getDevice(element);
                    if (!device.isInstrument)
                        continue;

                    te::Plugin* sourcePlugin = instrumentRackManager_.getRackInstance(device.id);
                    if (!sourcePlugin) {
                        juce::ScopedLock lock(pluginLock_);
                        auto it = findSyncedDevice(
                            ChainNodePath::topLevelDevice(sourceTrackId, device.id));
                        if (it != syncedDevices_.end())
                            sourcePlugin = it->second.plugin.get();
                    }

                    const int idx = pluginIndex(sourcePlugin);
                    if (idx >= 0)
                        return idx + 1;
                } else if (isRack(element)) {
                    const auto& rack = getRack(element);
                    if (!rackContainsInstrumentSource(rack))
                        continue;

                    const int idx = pluginIndex(rackSyncManager_.getRackInstance(rack.id));
                    if (idx >= 0)
                        return idx + 1;
                }
            }
        }

        return frontInsertPos();
    };

    if (audioSidechainMonitors_.count(sourceTrackId) > 0) {
        int existingIndex = -1;
        for (int i = 0; i < teTrack->pluginList.size(); ++i) {
            if (teTrack->pluginList[i] == audioSidechainMonitors_[sourceTrackId].get()) {
                existingIndex = i;
                break;
            }
        }

        MAGDA_ADSR_AUDIO_LOG("monitor already exists sourceTrack="
                             << sourceTrackId << " pluginIndex=" << existingIndex);

        const int desiredIndex = computeInsertPos();
        if (existingIndex == desiredIndex)
            return;

        if (auto* plugin = audioSidechainMonitors_[sourceTrackId].get())
            plugin->deleteFromParent();
        audioSidechainMonitors_.erase(sourceTrackId);
        MAGDA_ADSR_AUDIO_LOG("removed monitor for trigger-tap reposition sourceTrack="
                             << sourceTrackId << " oldIndex=" << existingIndex
                             << " desiredIndex=" << desiredIndex);
    }

    // Check if an AudioSidechainMonitorPlugin already exists on the track
    for (int i = 0; i < teTrack->pluginList.size(); ++i) {
        if (dynamic_cast<AudioSidechainMonitorPlugin*>(teTrack->pluginList[i])) {
            const int desiredIndex = computeInsertPos();
            if (i != desiredIndex) {
                teTrack->pluginList[i]->deleteFromParent();
                MAGDA_ADSR_AUDIO_LOG(
                    "removed existing monitor for trigger-tap reposition sourceTrack="
                    << sourceTrackId << " oldIndex=" << i << " desiredIndex=" << desiredIndex);
                break;
            }

            DBG("PluginManager::ensureAudioSidechainMonitor - track "
                << sourceTrackId << " found existing audio monitor plugin on TE track");
            audioSidechainMonitors_[sourceTrackId] = teTrack->pluginList[i];
            auto* mon = dynamic_cast<AudioSidechainMonitorPlugin*>(teTrack->pluginList[i]);
            mon->setSourceTrackId(sourceTrackId);
            mon->setPluginManager(this);
            MAGDA_ADSR_AUDIO_LOG("using existing monitor sourceTrack=" << sourceTrackId
                                                                       << " pluginIndex=" << i);
            return;
        }
    }

    // Create a new audio monitor plugin
    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, AudioSidechainMonitorPlugin::xmlTypeName, nullptr);
    pluginState.setProperty(juce::Identifier("sourceTrackId"), sourceTrackId, nullptr);

    DBG("PluginManager::ensureAudioSidechainMonitor - creating new audio monitor for track "
        << sourceTrackId);
    MAGDA_ADSR_AUDIO_LOG("creating monitor sourceTrack=" << sourceTrackId);
    auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    if (plugin) {
        if (auto* mon = dynamic_cast<AudioSidechainMonitorPlugin*>(plugin.get())) {
            mon->setSourceTrackId(sourceTrackId);
            mon->setPluginManager(this);
        }
        int insertPos = computeInsertPos();
        teTrack->pluginList.insertPlugin(plugin, insertPos, nullptr);
        audioSidechainMonitors_[sourceTrackId] = plugin;
        DBG("PluginManager::ensureAudioSidechainMonitor - inserted audio monitor at position "
            << insertPos << " on track " << sourceTrackId);
        MAGDA_ADSR_AUDIO_LOG("inserted monitor sourceTrack=" << sourceTrackId
                                                             << " insertPos=" << insertPos);
    } else {
        DBG("PluginManager::ensureAudioSidechainMonitor - FAILED to create audio monitor for track "
            << sourceTrackId);
        MAGDA_ADSR_AUDIO_LOG("failed creating monitor sourceTrack=" << sourceTrackId);
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

// =============================================================================
// Follower Source Tap Plugin Lifecycle (post-FX band-limit feed)
// =============================================================================

bool PluginManager::trackNeedsFollowerSourceTap(TrackId trackId) const {
    if (trackId < 0 || trackId >= kMaxCacheTracks)
        return false;

    auto* cache = activeCache_.load(std::memory_order_acquire);
    const bool needed = cache && cache->entries[static_cast<size_t>(trackId)].hasFollowerSource;
    if (needed) {
        const auto& entry = cache->entries[static_cast<size_t>(trackId)];
        MAGDA_ADSR_AUDIO_LOG("follower-tap-needed sourceTrack=" << trackId << " followerCount="
                                                                << entry.followerCount);
    }
    return needed;
}

void PluginManager::ensureFollowerSourceTap(TrackId sourceTrackId) {
    auto* teTrack = trackController_.getAudioTrack(sourceTrackId);
    if (!teTrack) {
        MAGDA_ADSR_AUDIO_LOG("follower-tap ensure-missing-track sourceTrack=" << sourceTrackId);
        return;
    }

    // Post-FX, post-fader: insert just before the track's final LevelMeter so the
    // follower tracks the track's processed output (the same point a sidechain
    // bus taps). Falls back to end-of-chain when there's no meter yet.
    auto desiredPos = [&]() {
        for (int i = 0; i < teTrack->pluginList.size(); ++i)
            if (dynamic_cast<te::LevelMeterPlugin*>(teTrack->pluginList[i]))
                return i;
        return teTrack->pluginList.size();
    };

    // Reuse an existing tap if it's already at the desired position; otherwise
    // drop it so we can reinsert cleanly (chain may have grown since).
    auto reuseExisting = [&](te::Plugin* existing, int existingIndex) {
        followerSourceTaps_[sourceTrackId] = existing;
        if (auto* tap = dynamic_cast<FollowerSourceTapPlugin*>(existing)) {
            tap->setSourceTrackId(sourceTrackId);
            tap->setPluginManager(this);
        }
        MAGDA_ADSR_AUDIO_LOG("follower-tap reuse sourceTrack=" << sourceTrackId
                                                               << " index=" << existingIndex
                                                               << " desired=" << desiredPos());
    };

    if (followerSourceTaps_.count(sourceTrackId) > 0) {
        auto* existing = followerSourceTaps_[sourceTrackId].get();
        int existingIndex = existing ? teTrack->pluginList.indexOf(existing) : -1;
        if (existingIndex >= 0 && existingIndex == desiredPos() - 1) {
            reuseExisting(existing, existingIndex);
            return;
        }
        if (existing) {
            MAGDA_ADSR_AUDIO_LOG("follower-tap remove-for-reposition sourceTrack="
                                 << sourceTrackId << " oldIndex=" << existingIndex
                                 << " desired=" << desiredPos());
            existing->deleteFromParent();
        }
        followerSourceTaps_.erase(sourceTrackId);
    }

    // Adopt a stray tap already on the TE track (e.g. restored from state).
    for (int i = 0; i < teTrack->pluginList.size(); ++i) {
        if (dynamic_cast<FollowerSourceTapPlugin*>(teTrack->pluginList[i])) {
            if (i == desiredPos() - 1) {
                reuseExisting(teTrack->pluginList[i], i);
                return;
            }
            MAGDA_ADSR_AUDIO_LOG("follower-tap remove-stray sourceTrack="
                                 << sourceTrackId << " oldIndex=" << i
                                 << " desired=" << desiredPos());
            teTrack->pluginList[i]->deleteFromParent();
            break;
        }
    }

    juce::ValueTree pluginState(te::IDs::PLUGIN);
    pluginState.setProperty(te::IDs::type, FollowerSourceTapPlugin::xmlTypeName, nullptr);
    pluginState.setProperty(juce::Identifier("sourceTrackId"), sourceTrackId, nullptr);

    auto plugin = edit_.getPluginCache().createNewPlugin(pluginState);
    if (plugin) {
        if (auto* tap = dynamic_cast<FollowerSourceTapPlugin*>(plugin.get())) {
            tap->setSourceTrackId(sourceTrackId);
            tap->setPluginManager(this);
        }
        const int insertPos = desiredPos();
        teTrack->pluginList.insertPlugin(plugin, insertPos, nullptr);
        followerSourceTaps_[sourceTrackId] = plugin;
        MAGDA_ADSR_AUDIO_LOG("follower-tap inserted sourceTrack="
                             << sourceTrackId << " index=" << insertPos
                             << " pluginCount=" << teTrack->pluginList.size());
    } else {
        MAGDA_ADSR_AUDIO_LOG("follower-tap create-failed sourceTrack=" << sourceTrackId);
    }
}

void PluginManager::removeFollowerSourceTap(TrackId sourceTrackId) {
    auto it = followerSourceTaps_.find(sourceTrackId);
    if (it == followerSourceTaps_.end())
        return;

    auto* plugin = it->second.get();
    followerSourceTaps_.erase(it);

    if (plugin) {
        MAGDA_ADSR_AUDIO_LOG("follower-tap removed sourceTrack=" << sourceTrackId);
        plugin->deleteFromParent();
    }
}

}  // namespace magda
