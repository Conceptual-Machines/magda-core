#include <algorithm>
#include <set>
#include <unordered_set>
#include <vector>

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
#include "processors/DeviceProcessor.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

te::Plugin* PluginManager::lookupTargetPluginForModifier(DeviceId id) const {
    te::Plugin::Ptr plugin;
    {
        juce::ScopedLock lock(pluginLock_);
        auto sdIt = syncedDevices_.find(id);
        if (sdIt != syncedDevices_.end())
            plugin = sdIt->second.plugin;
    }
    if (plugin)
        return plugin.get();
    return instrumentRackManager_.getInnerPlugin(id);
}

namespace {

// Adapter exposing PluginManager's syncedDevices_ + instrument-rack manager
// as a TargetPluginLookup. Stored on the stack at the call site; non-owning.
struct DeviceTargetLookup : TargetPluginLookup {
    const PluginManager& pm;
    explicit DeviceTargetLookup(const PluginManager& p) : pm(p) {}
    te::Plugin* getPlugin(DeviceId id) const override {
        return pm.lookupTargetPluginForModifier(id);
    }
};

}  // namespace

// =============================================================================
// Device-Level Modifier Sync
// =============================================================================

void PluginManager::updateDeviceModifierProperties(TrackId trackId) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    DeviceTargetLookup lookup(*this);

    auto forEachPlugin = [&, this](const std::function<void(te::Plugin*)>& visit) {
        for (int pi = 0; pi < teTrack->pluginList.size(); ++pi) {
            if (auto* plugin = teTrack->pluginList[pi])
                visit(plugin);
        }
        for (const auto& el : trackInfo->chainElements) {
            if (!isDevice(el))
                continue;
            const auto& dev = getDevice(el);
            if (dev.isInstrument) {
                if (auto* inner = instrumentRackManager_.getInnerPlugin(dev.id))
                    visit(inner);
            }
        }
        for (const auto& [drumGridDevId, padDevIds] : drumGridPadDevices_) {
            auto sdIt = syncedDevices_.find(drumGridDevId);
            if (sdIt == syncedDevices_.end() || sdIt->second.trackId != trackId)
                continue;
            for (auto padDevId : padDevIds) {
                te::Plugin::Ptr plugin;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pIt = syncedDevices_.find(padDevId);
                    if (pIt != syncedDevices_.end())
                        plugin = pIt->second.plugin;
                }
                if (plugin)
                    visit(plugin.get());
            }
        }
    };

    ModifierSyncContext ctx;
    ctx.lookup = &lookup;
    ctx.forEachScopePlugin = forEachPlugin;

    // Per-device: in-place LFO + assignment depth update.
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);
        auto sdIt = syncedDevices_.find(device.id);
        if (sdIt == syncedDevices_.end())
            continue;

        ConstChainNode node;
        node.scope = ChainScope::Device;
        node.deviceId = device.id;
        node.trackId = trackId;
        node.mods = &device.mods;
        node.macros = &device.macros;

        auto& sd = sdIt->second;
        ModifierSyncState state{sd.modifiers, sd.curveSnapshots, sd.macroParams};
        ModifierSyncWalker::syncProperties(node, ctx, state);
    }

    // Track-level.
    auto tmIt = trackModStates_.find(trackId);
    auto tmpIt = trackMacroParams_.find(trackId);
    if (tmIt == trackModStates_.end() && tmpIt == trackMacroParams_.end())
        return;

    auto& trackModState = trackModStates_[trackId];
    auto& trackMacroMap = trackMacroParams_[trackId];

    ConstChainNode trackNode;
    trackNode.scope = ChainScope::Track;
    trackNode.trackId = trackId;
    trackNode.mods = &trackInfo->mods;
    trackNode.macros = &trackInfo->macros;

    ModifierSyncState trackState{trackModState.modifiers, trackModState.curveSnapshots,
                                 trackMacroMap};
    ModifierSyncWalker::syncProperties(trackNode, ctx, trackState);
}

void PluginManager::syncDeviceModifiers(TrackId trackId, te::AudioTrack* teTrack) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    // Visit every plugin where stale modifier/macro assignments may need
    // scrubbing on rebuild — TE plugins on the track itself, instrument-rack
    // inner plugins, and DrumGrid pad-chain plugins. Includes the latter even
    // though pre-step-2 syncDeviceModifiers didn't scrub them; over-scrubbing
    // is a no-op and matches what syncDeviceMacros has always done.
    auto forEachPlugin = [&, this](const std::function<void(te::Plugin*)>& visit) {
        for (int pi = 0; pi < teTrack->pluginList.size(); ++pi) {
            if (auto* plugin = teTrack->pluginList[pi])
                visit(plugin);
        }
        for (const auto& el : trackInfo->chainElements) {
            if (!isDevice(el))
                continue;
            const auto& dev = getDevice(el);
            if (dev.isInstrument) {
                if (auto* inner = instrumentRackManager_.getInnerPlugin(dev.id))
                    visit(inner);
            }
        }
        for (const auto& [drumGridDevId, padDevIds] : drumGridPadDevices_) {
            auto sdIt = syncedDevices_.find(drumGridDevId);
            if (sdIt == syncedDevices_.end() || sdIt->second.trackId != trackId)
                continue;
            for (auto padDevId : padDevIds) {
                te::Plugin::Ptr plugin;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pIt = syncedDevices_.find(padDevId);
                    if (pIt != syncedDevices_.end())
                        plugin = pIt->second.plugin;
                }
                if (plugin)
                    visit(plugin.get());
            }
        }
    };

    DeviceTargetLookup lookup(*this);

    // ---- Per-device mods + macros ----
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);

        // Instruments live in their own InstrumentRack and need its modifier
        // list. Standalone plugins live directly on the track's modList.
        te::ModifierList* modList = nullptr;
        if (device.isInstrument) {
            if (auto rackType = instrumentRackManager_.getRackType(device.id))
                modList = &rackType->getModifierList();
        }
        if (!modList)
            modList = teTrack->getModifierList();

        // Bypassed devices: tear down any existing TE state, no rebuild.
        // Walker handles this uniformly when node.mods/macros are nullptr.
        ConstChainNode node;
        node.scope = ChainScope::Device;
        node.deviceId = device.id;
        node.trackId = trackId;
        node.mods = device.bypassed ? nullptr : &device.mods;
        node.macros = device.bypassed ? nullptr : &device.macros;

        ModifierSyncContext ctx;
        ctx.modifierList = modList;
        ctx.macroList = &teTrack->getMacroParameterListForWriting();
        ctx.lookup = &lookup;
        ctx.forEachScopePlugin = forEachPlugin;
        ctx.hasCrossTrackSidechain = device.sidechain.sourceTrackId != INVALID_TRACK_ID;

        auto& sd = syncedDevices_[device.id];
        ModifierSyncState state{sd.modifiers, sd.curveSnapshots, sd.macroParams};
        ModifierSyncWalker::syncStructure(node, ctx, state, deferredHolders_);
    }

    // ---- Track-level mods + macros ----
    auto& trackModState = trackModStates_[trackId];
    auto& trackMacroMap = trackMacroParams_[trackId];

    ConstChainNode trackNode;
    trackNode.scope = ChainScope::Track;
    trackNode.trackId = trackId;
    trackNode.mods = &trackInfo->mods;
    trackNode.macros = &trackInfo->macros;

    ModifierSyncContext trackCtx;
    trackCtx.modifierList = teTrack->getModifierList();
    trackCtx.macroList = &teTrack->getMacroParameterListForWriting();
    trackCtx.lookup = &lookup;
    trackCtx.forEachScopePlugin = forEachPlugin;
    trackCtx.hasCrossTrackSidechain = false;

    ModifierSyncState trackState{trackModState.modifiers, trackModState.curveSnapshots,
                                 trackMacroMap};
    ModifierSyncWalker::syncStructure(trackNode, trackCtx, trackState, deferredHolders_);
}

// =============================================================================
void PluginManager::triggerLFONoteOn(TrackId trackId) {
    // Trigger resync on all TE LFO modifiers associated with devices on this track
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo)
        return;

    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);

        // Skip sidechain LFOs — they are triggered separately via
        // triggerSidechainNoteOn from the source track's monitor plugin.
        // Triggering them here would reset the TE LFO phase mid-cycle,
        // causing false wrap-around detection in one-shot mode.
        if (device.sidechain.sourceTrackId != INVALID_TRACK_ID)
            continue;

        auto it = syncedDevices_.find(device.id);
        if (it == syncedDevices_.end())
            continue;

        for (auto& [_modId, mod] : it->second.modifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                triggerLFONoteOnWithReset(lfo);
            }
        }
    }

    // Also trigger LFOs inside MAGDA racks on this track
    rackSyncManager_.triggerLFONoteOn(trackId);

    // Also trigger track-level LFOs
    auto tmIt = trackModStates_.find(trackId);
    if (tmIt != trackModStates_.end()) {
        for (auto& [_modId, mod] : tmIt->second.modifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                triggerLFONoteOnWithReset(lfo);
            }
        }
    }
}

// =============================================================================
void PluginManager::triggerSidechainNoteOn(TrackId sourceTrackId,
                                           std::optional<LFOTriggerMode> modeFilter) {
    if (sourceTrackId < 0 || sourceTrackId >= kMaxCacheTracks)
        return;

    auto* cache = activeCache_.load(std::memory_order_acquire);
    auto& entry = cache->entries[static_cast<size_t>(sourceTrackId)];
    for (int i = 0; i < entry.count; ++i) {
        // Filter by trigger mode if specified
        if (modeFilter.has_value() && entry.trigMode[static_cast<size_t>(i)] != modeFilter.value())
            continue;

        auto* lfo = entry.lfos[static_cast<size_t>(i)];
        bool crossTrack = entry.isCrossTrack[static_cast<size_t>(i)];
        // Cross-track: force value=0 for transient gap.
        // Self-track: resync phase but preserve value (no zero gap needed).
        triggerLFONoteOnWithReset(lfo, crossTrack);
    }
}

void PluginManager::gateSidechainLFOs(TrackId sourceTrackId) {
    if (sourceTrackId < 0 || sourceTrackId >= kMaxCacheTracks)
        return;

    // Skip gating during offline rendering — the renderer processes much faster
    // than real-time, so noteOn/noteOff arrive in the same block and the gate
    // would kill the LFO before it can run its curve.
    if (renderingActive_.load(std::memory_order_relaxed))
        return;

    auto* cache = activeCache_.load(std::memory_order_acquire);
    auto& entry = cache->entries[static_cast<size_t>(sourceTrackId)];
    for (int i = 0; i < entry.count; ++i) {
        // Only gate cross-track (sidechain destination) LFOs.
        // Self-track LFOs should free-run and just reset phase on noteOn.
        if (!entry.isCrossTrack[static_cast<size_t>(i)])
            continue;
        auto* lfo = entry.lfos[static_cast<size_t>(i)];
        // Only gate note-triggered LFOs (syncType == 2)
        if (juce::roundToInt(lfo->syncTypeParam->getCurrentValue()) == 2) {
            DBG("[SC-GATE] gating LFO srcTrack=" << sourceTrackId << " idx=" << i);
            lfo->setGated(true);
        }
    }
}

void PluginManager::prepareForRendering() {
    renderingActive_.store(true, std::memory_order_release);
    rackSyncManager_.setRenderingActive(true);

    DBG("[RENDER] prepareForRendering called");

    // Reset sidechain monitors so held-note counts from playback don't
    // carry over into the render pass (which would prevent gating).
    for (auto& [trackId, pluginPtr] : sidechainMonitors_) {
        if (auto* monitor = dynamic_cast<SidechainMonitorPlugin*>(pluginPtr.get())) {
            DBG("[RENDER] resetting MIDI sidechain monitor for track " << trackId);
            monitor->reset();
        }
    }
    for (auto& [trackId, pluginPtr] : audioSidechainMonitors_) {
        if (auto* monitor = dynamic_cast<AudioSidechainMonitorPlugin*>(pluginPtr.get())) {
            DBG("[RENDER] resetting audio sidechain monitor for track " << trackId);
            monitor->reset();
        }
    }

    // Re-enable tone generators that were bypassed when transport stopped.
    // Must happen before the render graph is built so they appear as enabled.
    {
        juce::ScopedLock lock(pluginLock_);
        for (const auto& [deviceId, sd] : syncedDevices_) {
            if (auto* toneProc = dynamic_cast<ToneGeneratorProcessor*>(sd.processor.get())) {
                toneProc->setBypassed(false);
            }
        }
    }

    // Un-gate all MIDI/Audio-triggered LFOs so modulation is active during
    // offline rendering. The sidechain monitor triggers still fire, but the
    // message-thread gate management can't keep up with render speed.
    {
        juce::ScopedLock lock(pluginLock_);
        for (auto& [deviceId, sd] : syncedDevices_) {
            for (auto& [_modId, mod] : sd.modifiers) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                    bool wasGated = lfo->isGated();
                    lfo->setGated(false);
                    DBG("[RENDER] un-gated device LFO devId="
                        << deviceId << " wasGated=" << (int)wasGated
                        << " syncType=" << juce::roundToInt(lfo->syncTypeParam->getCurrentValue())
                        << " curValue=" << lfo->getCurrentValue());
                }
            }
        }
    }
    for (auto& [trackId, tms] : trackModStates_) {
        for (auto& [_modId, mod] : tms.modifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                bool wasGated = lfo->isGated();
                lfo->setGated(false);
                DBG("[RENDER] un-gated track LFO trackId="
                    << trackId << " wasGated=" << (int)wasGated
                    << " syncType=" << juce::roundToInt(lfo->syncTypeParam->getCurrentValue())
                    << " curValue=" << lfo->getCurrentValue());
            }
        }
    }
    rackSyncManager_.ungateAllLFOs();

    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        updateDeviceModifierProperties(track.id);
        rackSyncManager_.updateAllModifierProperties(track.id);
    }

    // Log assignment state after update
    {
        juce::ScopedLock lock(pluginLock_);
        for (auto& [deviceId, sd] : syncedDevices_) {
            size_t modIdx = 0;
            for (auto& [_modId, mod] : sd.modifiers) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                    DBG("[RENDER] post-update device LFO devId="
                        << deviceId << " modIdx=" << modIdx << " gated=" << (int)lfo->isGated()
                        << " curValue=" << lfo->getCurrentValue()
                        << " depth=" << lfo->depthParam->getCurrentValue()
                        << " wave=" << juce::roundToInt(lfo->waveParam->getCurrentValue()));
                }
                ++modIdx;
            }
        }
    }

    DBG("[RENDER] prepareForRendering done, monitors=" << sidechainMonitors_.size());
}

void PluginManager::restoreAfterRendering() {
    renderingActive_.store(false, std::memory_order_release);
    rackSyncManager_.setRenderingActive(false);
    // LFOs were un-gated for rendering. They'll be re-gated naturally by
    // gateSidechainLFOs on the next note-off, or by syncDeviceModifiers
    // if the edit is rebuilt.
}

void PluginManager::resetSidechainState() {
    // 1. Reset all sidechain monitors (zeroes localHeldNoteCount_ and bus)
    for (auto& [trackId, pluginPtr] : sidechainMonitors_) {
        if (auto* monitor = dynamic_cast<SidechainMonitorPlugin*>(pluginPtr.get()))
            monitor->reset();
    }
    for (auto& [trackId, pluginPtr] : audioSidechainMonitors_) {
        if (auto* monitor = dynamic_cast<AudioSidechainMonitorPlugin*>(pluginPtr.get()))
            monitor->reset();
    }

    // 2. Re-gate all triggered LFOs (those with skipNativeResync).
    {
        juce::ScopedLock lock(pluginLock_);
        for (auto& [deviceId, sd] : syncedDevices_) {
            for (auto& [_modId, mod] : sd.modifiers) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                    if (lfo->getSkipNativeResync())
                        lfo->setGated(true);
                }
            }
        }
    }

    // 3. Re-gate triggered LFOs on track-level modifiers
    for (auto& [trackId, tms] : trackModStates_) {
        for (auto& [_modId, mod] : tms.modifiers) {
            if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                if (lfo->getSkipNativeResync())
                    lfo->setGated(true);
            }
        }
    }

    // 4. Re-gate triggered LFOs inside racks
    rackSyncManager_.regateTriggeredLFOs();
}

void PluginManager::syncLFOValuesToVisuals() {
    auto& tm = TrackManager::getInstance();

    // Only overlay when the local sim considers this mod "running" — see
    // RackSyncManager::syncLFOValuesToVisuals for full rationale. The audio
    // LFO keeps free-running in syncType=note mode, but the visual must
    // freeze on note release.
    auto overlayMod = [](ModInfo& magdaMod, te::Modifier::Ptr& mod) {
        const bool running = (magdaMod.triggerMode == LFOTriggerMode::Free) || magdaMod.running;
        if (!running)
            return;
        if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
            magdaMod.value = lfo->getCurrentValue();
            magdaMod.phase = lfo->getCurrentPhase();
        }
    };

    // Track-level mods.
    for (auto& [trackId, tms] : trackModStates_) {
        if (tms.modifiers.empty())
            continue;
        auto* trackInfo = tm.getTrack(trackId);
        if (!trackInfo)
            continue;
        for (auto& magdaMod : trackInfo->mods) {
            auto it = tms.modifiers.find(magdaMod.id);
            if (it == tms.modifiers.end() || !it->second)
                continue;
            overlayMod(magdaMod, it->second);
        }
    }

    // Top-level device mods (and instrument-rack inner devices, since both
    // live in syncedDevices_ keyed by DeviceId).
    {
        juce::ScopedLock lock(pluginLock_);
        for (auto& [deviceId, sd] : syncedDevices_) {
            if (sd.modifiers.empty())
                continue;
            auto* device = tm.getDevice(sd.trackId, deviceId);
            if (!device)
                continue;
            for (auto& magdaMod : device->mods) {
                auto it = sd.modifiers.find(magdaMod.id);
                if (it == sd.modifiers.end() || !it->second)
                    continue;
                overlayMod(magdaMod, it->second);
            }
        }
    }

    // Rack-internal mods.
    rackSyncManager_.syncLFOValuesToVisuals();
}

void PluginManager::rebuildSidechainLFOCache() {
    auto& tm = TrackManager::getInstance();

    // Build into the inactive buffer, then atomically swap the pointer.
    // Audio thread reads through activeCache_ with acquire — no lock needed.
    auto& newCache = cacheBuffers_[writeCacheIndex_].entries;
    // Zero out the inactive buffer before populating
    for (auto& e : newCache)
        e = PerTrackEntry{};

    for (const auto& track : tm.getTracks()) {
        if (track.id < 0 || track.id >= kMaxCacheTracks)
            continue;

        auto& entry = newCache[static_cast<size_t>(track.id)];
        std::vector<te::LFOModifier*> lfos;
        std::vector<LFOTriggerMode> modes;
        int selfTrackCount = 0;  // track how many are self-track (added first)

        // Helper to collect LFOs from a device's synced modifiers, pairing
        // each with the trigger mode from the MAGDA ModInfo. ModId-keyed
        // lookup post-step-2 (the prior positional walk silently misaligned
        // when an enabled-but-linkless mod sat between two enabled+linked
        // ones, since the MAGDA-side filter advanced past it but the TE-side
        // index didn't).
        auto collectDeviceLFOs = [&](const DeviceInfo& device) {
            auto it = syncedDevices_.find(device.id);
            if (it == syncedDevices_.end())
                return;
            for (const auto& modInfo : device.mods) {
                const bool hasEnabledLinks =
                    std::any_of(modInfo.links.begin(), modInfo.links.end(),
                                [](const ModLink& link) { return link.enabled; });
                if (!modInfo.enabled || !hasEnabledLinks)
                    continue;
                auto modIt = it->second.modifiers.find(modInfo.id);
                if (modIt == it->second.modifiers.end() || !modIt->second)
                    continue;
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(modIt->second.get())) {
                    lfos.push_back(lfo);
                    modes.push_back(modInfo.triggerMode);
                }
            }
        };

        // 1. Self-track LFOs: collect from syncedDevices_ modifiers for this track's devices
        //    Skip devices that have a cross-track sidechain source — those LFOs
        //    are triggered by the source track, not by self.
        for (const auto& element : track.chainElements) {
            if (!isDevice(element))
                continue;
            const auto& device = getDevice(element);
            if (device.sidechain.sourceTrackId != INVALID_TRACK_ID)
                continue;  // Has external sidechain — skip self-triggering
            collectDeviceLFOs(device);
        }

        // Also collect from racks on this track. If any nested rack scope has
        // an external sidechain source, the rack manager will collect matching
        // LFOs in the source-track pass below.
        if (!sidechain::elementsContainExternalSource(track.chainElements))
            rackSyncManager_.collectLFOModifiersWithModes(track.id, lfos, modes);

        selfTrackCount = static_cast<int>(lfos.size());

        // 2. Cross-track LFOs: for each OTHER track that has a device or rack
        //    sidechained from this track, collect only the destination LFOs
        //    whose sidechain source resolves to this track.
        for (const auto& otherTrack : tm.getTracks()) {
            if (otherTrack.id == track.id)
                continue;

            if (!sidechain::elementsUseSource(otherTrack.chainElements, track.id))
                continue;

            // Collect LFO modifiers only from devices on the destination track
            // that are actually sidechained from this source track.
            for (const auto& element : otherTrack.chainElements) {
                if (!isDevice(element))
                    continue;
                const auto& device = getDevice(element);
                // Only collect from devices whose sidechain source is this track
                if (device.sidechain.sourceTrackId != track.id)
                    continue;
                collectDeviceLFOs(device);
            }
            rackSyncManager_.collectLFOModifiersWithModesForSidechainSource(otherTrack.id, track.id,
                                                                            lfos, modes);
        }

        // Write to cache entry (capped at kMaxLFOs)
        // Self-track LFOs come first (indices 0..selfTrackCount-1),
        // cross-track LFOs follow (indices selfTrackCount..count-1).
        entry.count = std::min(static_cast<int>(lfos.size()), PerTrackEntry::kMaxLFOs);
        for (int i = 0; i < entry.count; ++i) {
            entry.lfos[static_cast<size_t>(i)] = lfos[static_cast<size_t>(i)];
            entry.isCrossTrack[static_cast<size_t>(i)] = (i >= selfTrackCount);
            entry.trigMode[static_cast<size_t>(i)] = (static_cast<size_t>(i) < modes.size())
                                                         ? modes[static_cast<size_t>(i)]
                                                         : LFOTriggerMode::Free;
        }
    }

    // Atomically swap to the newly built buffer. The audio thread will see
    // the new cache on its next read via acquire on activeCache_.
    activeCache_.store(&cacheBuffers_[writeCacheIndex_], std::memory_order_release);
    writeCacheIndex_ = 1 - writeCacheIndex_;
}

// =============================================================================
std::pair<int, int> PluginManager::computeModLinkFingerprint(TrackId trackId,
                                                             const TrackInfo* trackInfo) const {
    if (!trackInfo)
        return {0, 0};

    int modCount = 0, linkCount = 0, bipolarCount = 0;

    // Device-level mods
    for (const auto& element : trackInfo->chainElements) {
        if (!isDevice(element))
            continue;
        const auto& device = getDevice(element);
        for (const auto& mod : device.mods) {
            int enabledLinkCount = 0;
            for (const auto& link : mod.links)
                enabledLinkCount += link.enabled ? 1 : 0;
            if (mod.enabled && enabledLinkCount > 0) {
                ++modCount;
                linkCount += enabledLinkCount;
                for (const auto& link : mod.links)
                    bipolarCount += (link.enabled && link.bipolar) ? 1 : 0;
            }
        }
        // Device-level macros
        for (const auto& macro : device.macros) {
            if (!macro.links.empty()) {
                ++modCount;
                linkCount += static_cast<int>(macro.links.size());
                for (const auto& link : macro.links)
                    bipolarCount += link.bipolar ? 1 : 0;
            }
        }
    }

    // Track-level mods
    for (const auto& mod : trackInfo->mods) {
        int enabledLinkCount = 0;
        for (const auto& link : mod.links)
            enabledLinkCount += link.enabled ? 1 : 0;
        if (mod.enabled && enabledLinkCount > 0) {
            ++modCount;
            linkCount += enabledLinkCount;
            for (const auto& link : mod.links)
                bipolarCount += (link.enabled && link.bipolar) ? 1 : 0;
        }
    }

    // Track-level macros
    for (const auto& macro : trackInfo->macros) {
        if (!macro.links.empty()) {
            ++modCount;
            linkCount += static_cast<int>(macro.links.size());
            for (const auto& link : macro.links)
                bipolarCount += link.bipolar ? 1 : 0;
        }
    }

    return {modCount, linkCount + (bipolarCount << 16)};
}

// =============================================================================
void PluginManager::resyncDeviceModifiers(TrackId trackId) {
    auto* teTrack = trackController_.getAudioTrack(trackId);
    if (teTrack) {
        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        auto currentFP = computeModLinkFingerprint(trackId, trackInfo);
        auto& storedFP = modLinkFingerprints_[trackId];

        if (currentFP != storedFP) {
            // During offline rendering, skip full rebuilds — syncDeviceModifiers
            // creates new TE LFOs that start gated, undoing prepareForRendering's
            // un-gating and silencing the sidechained track.
            if (renderingActive_.load(std::memory_order_relaxed)) {
                DBG("[RENDER] skipping full modifier rebuild for track " << trackId
                                                                         << " — rendering active");
                updateDeviceModifierProperties(trackId);
            } else {
                // Link structure changed — full rebuild
                storedFP = currentFP;
                syncDeviceModifiers(trackId, teTrack);
            }
        } else {
            // Properties only changed (rate, waveform, etc.) — update in-place
            updateDeviceModifierProperties(trackId);
        }
    }
    rackSyncManager_.resyncAllModifiers(trackId);
    rebuildSidechainLFOCache();
}

te::AutomatableParameter* PluginManager::findModifierParameterForAutomation(
    TrackId trackId, const ChainNodePath& devicePath, ModId modId, int modParamIndex) const {
    if (modId == INVALID_MOD_ID || modParamIndex < 0)
        return nullptr;

    // MAGDA exposes a single semantic Rate lane (modParamIndex 0). The TE
    // parameter we route automation to depends on the modifier's tempoSync
    // flag — Hz values bake into TE's `rate`, sync divisions into `rateType`.
    // Future depth lane lives at index 1.
    if (modParamIndex >= 2)
        return nullptr;

    // ModId is a per-array slot index, not globally unique. Pick the right
    // owner from the path before matching modId.
    auto resolveFromMap =
        [&](const std::vector<ModInfo>& mods,
            const std::map<ModId, te::Modifier::Ptr>& teMods) -> te::AutomatableParameter* {
        auto it = teMods.find(modId);
        if (it == teMods.end() || !it->second)
            return nullptr;
        bool sync = false;
        for (const auto& m : mods) {
            if (m.id == modId) {
                sync = m.tempoSync;
                break;
            }
        }
        const juce::String wantedID = modParamIndex == 0 ? (sync ? "rateType" : "rate") : "depth";
        for (auto* p : it->second->getAutomatableParameters()) {
            if (p && p->paramID == wantedID)
                return p;
        }
        return nullptr;
    };

    auto& tm = TrackManager::getInstance();

    if (devicePath.isValid()) {
        switch (devicePath.getType()) {
            case ChainNodeType::Rack:
                return rackSyncManager_.findRackModifierParameter(devicePath.getRackId(), modId,
                                                                  modParamIndex);
            case ChainNodeType::TopLevelDevice:
            case ChainNodeType::Device: {
                auto it = syncedDevices_.find(devicePath.getDeviceId());
                if (it == syncedDevices_.end())
                    return nullptr;
                DeviceInfo* device = tm.getDevice(it->second.trackId, it->first);
                if (!device)
                    return nullptr;
                return resolveFromMap(device->mods, it->second.modifiers);
            }
            default:
                break;
        }
    }

    // Track-scope modifier (no devicePath / track-level path).
    auto modIt = trackModStates_.find(trackId);
    if (modIt == trackModStates_.end())
        return nullptr;
    const TrackInfo* track = tm.getTrack(trackId);
    if (!track)
        return nullptr;
    return resolveFromMap(track->mods, modIt->second.modifiers);
}

}  // namespace magda
