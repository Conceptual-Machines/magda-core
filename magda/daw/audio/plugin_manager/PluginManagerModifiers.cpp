#include <algorithm>
#include <atomic>
#include <set>
#include <unordered_set>
#include <vector>

#include "../../core/InternalDeviceKind.hpp"
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
#include "plugins/MagdaSamplerPlugin.hpp"
#include "plugins/MidiChordEnginePlugin.hpp"
#include "plugins/MidiReceivePlugin.hpp"
#include "plugins/SidechainMonitorPlugin.hpp"
#include "plugins/StepSequencerPlugin.hpp"
#include "processors/DeviceProcessor.hpp"
#include "transport/TransportStateManager.hpp"

namespace magda {

te::Plugin* PluginManager::lookupTargetPluginForModifier(const ChainNodePath& devicePath) const {
    te::Plugin::Ptr plugin;
    {
        juce::ScopedLock lock(pluginLock_);
        auto sdIt = findSyncedDevice(devicePath);
        if (sdIt != syncedDevices_.end())
            plugin = sdIt->second.plugin;
    }
    if (plugin)
        return plugin.get();
    if (devicePath.topLevelDeviceId == INVALID_DEVICE_ID && !devicePath.isPostFx() &&
        !devicePath.isMixerAnalysis())
        return instrumentRackManager_.getInnerPlugin(devicePath.getDeviceId());
    return nullptr;
}

namespace {

// Adapter exposing PluginManager's syncedDevices_ + instrument-rack manager
// as a TargetPluginLookup. Stored on the stack at the call site; non-owning.
struct DeviceTargetLookup : TargetPluginLookup {
    const PluginManager& pm;
    explicit DeviceTargetLookup(const PluginManager& p) : pm(p) {}
    te::Plugin* getPlugin(const ChainNodePath& path) const override {
        return pm.lookupTargetPluginForModifier(path);
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
        for (const auto& el : trackInfo->chain.fxChainElements) {
            if (!isDevice(el))
                continue;
            const auto& dev = getDevice(el);
            if (dev.isInstrument) {
                if (auto* inner = instrumentRackManager_.getInnerPlugin(dev.id))
                    visit(inner);
            }
        }
        for (const auto& [drumGridPath, padPaths] : drumGridPadDevices_) {
            auto sdIt = findSyncedDevice(drumGridPath);
            if (sdIt == syncedDevices_.end() || sdIt->second.trackId != trackId)
                continue;
            for (const auto& padPath : padPaths) {
                te::Plugin::Ptr plugin;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pIt = findSyncedDevice(padPath);
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
    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);
        auto sdIt = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, device.id));
        if (sdIt == syncedDevices_.end())
            continue;

        ConstChainNode node;
        node.scope = ChainScope::Device;
        node.deviceId = device.id;
        node.trackId = trackId;
        node.mods = &device.mods;
        node.macros = &device.macros;

        // Per-device sidechain fact (drives the envelope follower's external
        // input). The in-place property path must set this too, since a
        // sidechain-source change keeps the same link fingerprint and so never
        // triggers a full rebuild.
        ctx.hasCrossTrackSidechain = device.sidechain.sourceTrackId != INVALID_TRACK_ID;

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

    ctx.hasCrossTrackSidechain = false;  // track-level mods have no device sidechain
    ModifierSyncState trackState{trackModState.modifiers, trackModState.curveSnapshots,
                                 trackMacroMap};
    ModifierSyncWalker::syncProperties(trackNode, ctx, trackState);
}

void PluginManager::syncDeviceModifiers(TrackId trackId, te::AudioTrack* teTrack) {
    auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
    if (!trackInfo || !teTrack)
        return;

    // This is the full teardown path: it destroys and recreates every TE modifier
    // on the track. The audio thread (FollowerSourceTapPlugin) dereferences this
    // track's envelope followers from the sidechain cache every block, so freeing
    // one mid-resync during playback is a use-after-free. Keep the followers about
    // to be torn down alive for one teardown-generation: release the previous
    // generation now (its pointers were dropped from the cache at the last rebuild,
    // so the audio thread has long since moved on) and stash the current ones,
    // which the rebuild at the end of this resync will replace in the cache.
    std::vector<te::Modifier::Ptr> releaseAfterSync = std::move(deferredFollowers_);
    deferredFollowers_.clear();
    auto stashFollowers = [this](const std::map<ModId, te::Modifier::Ptr>& mods) {
        for (const auto& [id, m] : mods)
            if (dynamic_cast<te::EnvelopeFollowerModifier*>(m.get()))
                deferredFollowers_.push_back(m);
    };
    for (const auto& el : trackInfo->chain.fxChainElements) {
        if (!isDevice(el))
            continue;
        auto it = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, getDevice(el).id));
        if (it != syncedDevices_.end())
            stashFollowers(it->second.modifiers);
    }
    if (auto tmIt = trackModStates_.find(trackId); tmIt != trackModStates_.end())
        stashFollowers(tmIt->second.modifiers);

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
        for (const auto& el : trackInfo->chain.fxChainElements) {
            if (!isDevice(el))
                continue;
            const auto& dev = getDevice(el);
            if (dev.isInstrument) {
                if (auto* inner = instrumentRackManager_.getInnerPlugin(dev.id))
                    visit(inner);
            }
        }
        for (const auto& [drumGridPath, padPaths] : drumGridPadDevices_) {
            auto sdIt = findSyncedDevice(drumGridPath);
            if (sdIt == syncedDevices_.end() || sdIt->second.trackId != trackId)
                continue;
            for (const auto& padPath : padPaths) {
                te::Plugin::Ptr plugin;
                {
                    juce::ScopedLock lock(pluginLock_);
                    auto pIt = findSyncedDevice(padPath);
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
    for (const auto& element : trackInfo->chain.fxChainElements) {
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
        // Analysis devices (oscilloscope / spectrum) are transparent passthroughs
        // and expose no macros or mods, so never sync TE modifier state for them.
        const bool analysis = ::magda::isAnalysisDevice(device.pluginId);
        node.mods = (device.bypassed || analysis) ? nullptr : &device.mods;
        node.macros = (device.bypassed || analysis) ? nullptr : &device.macros;

        ModifierSyncContext ctx;
        ctx.modifierList = modList;
        ctx.macroList = &teTrack->getMacroParameterListForWriting();
        ctx.lookup = &lookup;
        ctx.forEachScopePlugin = forEachPlugin;
        ctx.hasCrossTrackSidechain = device.sidechain.sourceTrackId != INVALID_TRACK_ID;

        auto& sd = syncedDevices_[ChainNodePath::topLevelDevice(trackId, device.id)];
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

    for (const auto& element : trackInfo->chain.fxChainElements) {
        if (!isDevice(element))
            continue;

        const auto& device = getDevice(element);

        // Skip sidechain LFOs — they are triggered separately via
        // triggerSidechainNoteOn from the source track's monitor plugin.
        // Triggering them here would reset the TE LFO phase mid-cycle,
        // causing false wrap-around detection in one-shot mode.
        if (device.sidechain.sourceTrackId != INVALID_TRACK_ID)
            continue;

        auto it = findSyncedDevice(ChainNodePath::topLevelDevice(trackId, device.id));
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
    MAGDA_ADSR_AUDIO_LOG("trigger dispatch sourceTrack="
                         << sourceTrackId << " cacheCount=" << entry.count << " modeFilter="
                         << (modeFilter.has_value() ? juce::String(static_cast<int>(*modeFilter))
                                                    : juce::String("none")));

    for (int i = 0; i < entry.count; ++i) {
        // Filter by trigger mode if specified
        if (modeFilter.has_value() &&
            entry.trigMode[static_cast<size_t>(i)] != modeFilter.value()) {
            MAGDA_ADSR_AUDIO_LOG("trigger skip sourceTrack="
                                 << sourceTrackId << " idx=" << i << " cachedMode="
                                 << static_cast<int>(entry.trigMode[static_cast<size_t>(i)]));
            continue;
        }

        auto* mod = entry.mods[static_cast<size_t>(i)];
        bool crossTrack = entry.isCrossTrack[static_cast<size_t>(i)];
        // Cross-track: force value=0 for transient gap.
        // Self-track: resync phase but preserve value (no zero gap needed).
        if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod)) {
            MAGDA_ADSR_AUDIO_LOG(
                "trigger LFO sourceTrack="
                << sourceTrackId << " idx=" << i << " crossTrack=" << static_cast<int>(crossTrack)
                << " mode=" << static_cast<int>(entry.trigMode[static_cast<size_t>(i)])
                << " gatedBefore=" << static_cast<int>(lfo->isGated())
                << " syncType=" << juce::roundToInt(lfo->syncTypeParam->getCurrentValue()));
            triggerLFONoteOnWithReset(lfo, crossTrack);
            MAGDA_ADSR_AUDIO_LOG("trigger LFO done sourceTrack="
                                 << sourceTrackId << " idx=" << i
                                 << " gatedAfter=" << static_cast<int>(lfo->isGated()));
        } else if (auto* adsr = dynamic_cast<te::ADSRModifier*>(mod)) {
            MAGDA_ADSR_AUDIO_LOG(
                "trigger ADSR sourceTrack="
                << sourceTrackId << " idx=" << i << " crossTrack=" << static_cast<int>(crossTrack)
                << " mode=" << static_cast<int>(entry.trigMode[static_cast<size_t>(i)])
                << " gatedBefore=" << static_cast<int>(adsr->isGated())
                << " syncType=" << juce::roundToInt(adsr->syncTypeParam->getCurrentValue())
                << " stageBefore=" << static_cast<int>(adsr->getCurrentStage())
                << " valueBefore=" << adsr->getCurrentValue());
            adsr->triggerNoteOn(crossTrack);
            MAGDA_ADSR_AUDIO_LOG("trigger ADSR done sourceTrack="
                                 << sourceTrackId << " idx=" << i
                                 << " gatedAfter=" << static_cast<int>(adsr->isGated())
                                 << " stageAfter=" << static_cast<int>(adsr->getCurrentStage())
                                 << " valueAfter=" << adsr->getCurrentValue());
        } else {
            MAGDA_ADSR_AUDIO_LOG("trigger unknown modifier sourceTrack=" << sourceTrackId
                                                                         << " idx=" << i);
        }
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
    MAGDA_ADSR_AUDIO_LOG("gate dispatch sourceTrack=" << sourceTrackId
                                                      << " cacheCount=" << entry.count);
    for (int i = 0; i < entry.count; ++i) {
        auto* mod = entry.mods[static_cast<size_t>(i)];

        // ADSR closes its gate on the level drop so the envelope releases, for
        // self- and cross-track alike (it is a one-shot envelope, not a loop).
        if (auto* adsr = dynamic_cast<te::ADSRModifier*>(mod)) {
            const int syncType = juce::roundToInt(adsr->syncTypeParam->getCurrentValue());
            MAGDA_ADSR_AUDIO_LOG("gate ADSR sourceTrack="
                                 << sourceTrackId << " idx=" << i << " mode="
                                 << static_cast<int>(entry.trigMode[static_cast<size_t>(i)])
                                 << " syncType=" << syncType
                                 << " gatedBefore=" << static_cast<int>(adsr->isGated())
                                 << " stageBefore=" << static_cast<int>(adsr->getCurrentStage())
                                 << " valueBefore=" << adsr->getCurrentValue());
            if (syncType == 2)
                adsr->setGated(true);
            MAGDA_ADSR_AUDIO_LOG("gate ADSR done sourceTrack="
                                 << sourceTrackId << " idx=" << i
                                 << " gatedAfter=" << static_cast<int>(adsr->isGated())
                                 << " stageAfter=" << static_cast<int>(adsr->getCurrentStage())
                                 << " valueAfter=" << adsr->getCurrentValue());
            continue;
        }

        // Only gate cross-track (sidechain destination) LFOs.
        // Self-track LFOs should free-run and just reset phase on noteOn.
        if (!entry.isCrossTrack[static_cast<size_t>(i)])
            continue;
        auto* lfo = dynamic_cast<te::LFOModifier*>(mod);
        // Only gate note-triggered LFOs (syncType == 2)
        if (lfo && juce::roundToInt(lfo->syncTypeParam->getCurrentValue()) == 2) {
            DBG("[SC-GATE] gating LFO srcTrack=" << sourceTrackId << " idx=" << i);
            MAGDA_ADSR_AUDIO_LOG("gate LFO sourceTrack="
                                 << sourceTrackId << " idx=" << i << " gatedBefore="
                                 << static_cast<int>(lfo->isGated()) << " syncType="
                                 << juce::roundToInt(lfo->syncTypeParam->getCurrentValue()));
            lfo->setGated(true);
            MAGDA_ADSR_AUDIO_LOG("gate LFO done sourceTrack=" << sourceTrackId << " idx=" << i
                                                              << " gatedAfter="
                                                              << static_cast<int>(lfo->isGated()));
        }
    }
}

void PluginManager::pushFollowerSourceBuffer(TrackId sourceTrackId, const float* mono,
                                             int numSamples, double sampleRate) {
    if (sourceTrackId < 0 || sourceTrackId >= kMaxCacheTracks || mono == nullptr ||
        numSamples <= 0) {
        static std::atomic<int> invalidLogThrottle{0};
        if ((invalidLogThrottle.fetch_add(1, std::memory_order_relaxed) % 200) == 0) {
            MAGDA_ADSR_AUDIO_LOG("follower-push invalid sourceTrack="
                                 << sourceTrackId
                                 << " hasMono=" << static_cast<int>(mono != nullptr)
                                 << " numSamples=" << numSamples);
        }
        return;
    }

    auto* cache = activeCache_.load(std::memory_order_acquire);
    auto& entry = cache->entries[static_cast<size_t>(sourceTrackId)];
    if (entry.followerCount <= 0) {
        static std::atomic<int> emptyLogThrottle{0};
        if ((emptyLogThrottle.fetch_add(1, std::memory_order_relaxed) % 200) == 0) {
            MAGDA_ADSR_AUDIO_LOG("follower-push no-followers sourceTrack="
                                 << sourceTrackId << " hasFollowerSource="
                                 << static_cast<int>(entry.hasFollowerSource)
                                 << " count=" << entry.followerCount);
        }
        return;
    }

    // Per-follower detection: apply input gain and that follower's HP/LP filters
    // (so each can track a different part of the spectrum), then take the peak
    // and stream it to the follower's envelope DSP.
    const int n = std::min(numSamples, static_cast<int>(followerScratch_.size()));
    float rawPeak = 0.0f;
    for (int s = 0; s < n; ++s)
        rawPeak = std::max(rawPeak, std::abs(mono[s]));

    static std::atomic<int> pushLogThrottle{0};
    const bool logThisBlock = (pushLogThrottle.fetch_add(1, std::memory_order_relaxed) % 100) == 0;
    if (logThisBlock) {
        MAGDA_ADSR_AUDIO_LOG("follower-push block sourceTrack="
                             << sourceTrackId << " followers=" << entry.followerCount << " samples="
                             << n << " rawPeak=" << rawPeak << " sampleRate=" << sampleRate);
    }

    for (int i = 0; i < entry.followerCount; ++i) {
        auto& slot = entry.followers[static_cast<size_t>(i)];
        if (slot.mod == nullptr) {
            if (logThisBlock)
                MAGDA_ADSR_AUDIO_LOG("follower-push slot-null sourceTrack=" << sourceTrackId
                                                                            << " slot=" << i);
            continue;
        }

        float peak = 0.0f;
        if (!slot.hpEnabled && !slot.lpEnabled) {
            for (int s = 0; s < n; ++s)
                peak = std::max(peak, std::abs(mono[s] * slot.gain));
        } else {
            float* work = followerScratch_.data();
            if (slot.gain == 1.0f) {
                std::copy(mono, mono + n, work);
            } else {
                for (int s = 0; s < n; ++s)
                    work[s] = mono[s] * slot.gain;
            }

            if (slot.hpEnabled) {
                if (slot.curHpFreq != slot.hpFreq) {
                    slot.hp.coeffs = juce::IIRCoefficients::makeHighPass(
                        sampleRate, juce::jlimit(20.0f, 20000.0f, slot.hpFreq));
                    slot.curHpFreq = slot.hpFreq;
                }
                slot.hp.process(work, n);
            }
            if (slot.lpEnabled) {
                if (slot.curLpFreq != slot.lpFreq) {
                    slot.lp.coeffs = juce::IIRCoefficients::makeLowPass(
                        sampleRate, juce::jlimit(20.0f, 20000.0f, slot.lpFreq));
                    slot.curLpFreq = slot.lpFreq;
                }
                slot.lp.process(work, n);
            }

            peak = 0.0f;
            for (int s = 0; s < n; ++s)
                peak = std::max(peak, std::abs(work[s]));
        }

        const float outBefore = slot.mod->getCurrentValue();
        slot.mod->setExternalInput(peak);
        if (logThisBlock) {
            MAGDA_ADSR_AUDIO_LOG("follower-push slot sourceTrack="
                                 << sourceTrackId << " slot=" << i << " rawPeak=" << rawPeak
                                 << " sentPeak=" << peak << " gain=" << slot.gain << " hpOn="
                                 << static_cast<int>(slot.hpEnabled) << " hpHz=" << slot.hpFreq
                                 << " lpOn=" << static_cast<int>(slot.lpEnabled)
                                 << " lpHz=" << slot.lpFreq << " outBefore=" << outBefore);
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
        for (auto& [devicePath, sd] : syncedDevices_) {
            for (auto& [_modId, mod] : sd.modifiers) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                    bool wasGated = lfo->isGated();
                    lfo->setGated(false);
                    DBG("[RENDER] un-gated device LFO devId="
                        << devicePath.getDeviceId() << " wasGated=" << (int)wasGated
                        << " syncType=" << juce::roundToInt(lfo->syncTypeParam->getCurrentValue())
                        << " curValue=" << lfo->getCurrentValue());
                } else {
                    setModifierGated(mod.get(), false);
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
            } else {
                setModifierGated(mod.get(), false);
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
        for (auto& [devicePath, sd] : syncedDevices_) {
            size_t modIdx = 0;
            for (auto& [_modId, mod] : sd.modifiers) {
                if (auto* lfo = dynamic_cast<te::LFOModifier*>(mod.get())) {
                    DBG("[RENDER] post-update device LFO devId="
                        << devicePath.getDeviceId() << " modIdx=" << modIdx << " gated="
                        << (int)lfo->isGated() << " curValue=" << lfo->getCurrentValue()
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
                if (modifierSkipsNativeResync(mod.get()))
                    setModifierGated(mod.get(), true);
            }
        }
    }

    // 3. Re-gate triggered modifiers on track-level modifiers
    for (auto& [trackId, tms] : trackModStates_) {
        for (auto& [_modId, mod] : tms.modifiers) {
            if (modifierSkipsNativeResync(mod.get()))
                setModifierGated(mod.get(), true);
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
        // The envelope value and the follower output are always meaningful, so
        // overlay them unconditionally (no trigger/running gating).
        if (magdaMod.type == ModType::Envelope || magdaMod.type == ModType::Follower) {
            overlayModifierVisuals(magdaMod, mod.get());
            return;
        }
        const bool running = (magdaMod.triggerMode == LFOTriggerMode::Free) || magdaMod.running;
        if (!running)
            return;
        overlayModifierVisuals(magdaMod, mod.get());
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
        for (auto& [devicePath, sd] : syncedDevices_) {
            if (sd.modifiers.empty())
                continue;
            auto* device = tm.getDeviceInChainByPath(devicePath);
            if (!device)
                device = tm.getDevice(sd.trackId, devicePath.getDeviceId());
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
        std::vector<te::Modifier*> lfos;  // gated modifiers: LFO + ADSR
        std::vector<LFOTriggerMode> modes;
        int selfTrackCount = 0;  // track how many are self-track (added first)
        // Envelope followers whose audio source is this track, paired with the
        // MAGDA ModInfo that carries their detection gain and HP/LP config.
        std::vector<std::pair<te::EnvelopeFollowerModifier*, const ModInfo*>> followerCollect;

        // Helper to collect gated modifiers from one MAGDA/TE modifier scope,
        // pairing each with the trigger mode from the MAGDA ModInfo. ModId-keyed
        // lookup avoids positional mismatches when enabled-but-linkless mods sit
        // between enabled+linked ones.
        auto collectGatedModifiers = [&](const std::vector<ModInfo>& magdaMods,
                                         const std::map<ModId, te::Modifier::Ptr>& teMods) {
            for (const auto& modInfo : magdaMods) {
                const bool hasEnabledLinks =
                    std::any_of(modInfo.links.begin(), modInfo.links.end(),
                                [](const ModLink& link) { return link.enabled; });
                if (!modInfo.enabled || !hasEnabledLinks)
                    continue;
                auto modIt = teMods.find(modInfo.id);
                if (modIt == teMods.end() || !modIt->second)
                    continue;
                if (dynamic_cast<te::LFOModifier*>(modIt->second.get()) ||
                    dynamic_cast<te::ADSRModifier*>(modIt->second.get())) {
                    MAGDA_ADSR_AUDIO_LOG(
                        "cache collect sourceTrack="
                        << track.id << " modId=" << static_cast<int>(modInfo.id) << " type="
                        << (dynamic_cast<te::ADSRModifier*>(modIt->second.get()) ? "ADSR" : "LFO")
                        << " mode=" << static_cast<int>(modInfo.triggerMode)
                        << " links=" << static_cast<int>(modInfo.links.size()));
                    lfos.push_back(modIt->second.get());
                    modes.push_back(modInfo.triggerMode);
                } else if (auto* ef =
                               dynamic_cast<te::EnvelopeFollowerModifier*>(modIt->second.get())) {
                    // Followers don't gate/trigger; they're fed a band-limited
                    // post-FX level by the FollowerSourceTapPlugin. Collect them
                    // separately with their MAGDA ModInfo (for detector config).
                    followerCollect.emplace_back(ef, &modInfo);
                }
            }
        };

        // deviceTrackId is the track the device actually lives on — the source
        // track for self-collection, the destination track for cross-track.
        auto collectDeviceLFOs = [&](const DeviceInfo& device, TrackId deviceTrackId) {
            auto it = findSyncedDevice(ChainNodePath::topLevelDevice(deviceTrackId, device.id));
            if (it != syncedDevices_.end())
                collectGatedModifiers(device.mods, it->second.modifiers);
        };

        auto elementsContainSourceOtherThanSelf =
            [&](auto&& self, const std::vector<ChainElement>& elements) -> bool {
            for (const auto& element : elements) {
                if (isDevice(element)) {
                    const auto sourceTrackId = getDevice(element).sidechain.sourceTrackId;
                    if (sourceTrackId != INVALID_TRACK_ID && sourceTrackId != track.id)
                        return true;
                } else if (isRack(element)) {
                    const auto& rack = getRack(element);
                    if (rack.sidechain.sourceTrackId != INVALID_TRACK_ID &&
                        rack.sidechain.sourceTrackId != track.id)
                        return true;
                    for (const auto& chain : rack.chains)
                        if (self(self, chain.elements))
                            return true;
                }
            }
            return false;
        };

        // 1. Self-track LFOs: collect from syncedDevices_ modifiers for this track's devices
        //    Skip devices that have a cross-track sidechain source — those LFOs
        //    are triggered by the source track, not by self.
        for (const auto& element : track.chain.fxChainElements) {
            if (!isDevice(element))
                continue;
            const auto& device = getDevice(element);
            if (device.sidechain.sourceTrackId != INVALID_TRACK_ID &&
                device.sidechain.sourceTrackId != track.id)
                continue;  // Has external sidechain — skip self-triggering
            collectDeviceLFOs(device, track.id);
        }

        // Also collect from racks on this track. If any nested rack scope has
        // an external sidechain source, the rack manager will collect matching
        // LFOs in the source-track pass below.
        if (!elementsContainSourceOtherThanSelf(elementsContainSourceOtherThanSelf,
                                                track.chain.fxChainElements))
            rackSyncManager_.collectLFOModifiersWithModes(track.id, lfos, modes);

        auto trackModsIt = trackModStates_.find(track.id);
        if (trackModsIt != trackModStates_.end())
            collectGatedModifiers(track.mods, trackModsIt->second.modifiers);

        selfTrackCount = static_cast<int>(lfos.size());

        // 2. Cross-track LFOs: for each OTHER track that has a device or rack
        //    sidechained from this track, collect only the destination LFOs
        //    whose sidechain source resolves to this track.
        for (const auto& otherTrack : tm.getTracks()) {
            if (otherTrack.id == track.id)
                continue;

            if (!sidechain::elementsUseSource(otherTrack.chain.fxChainElements, track.id))
                continue;

            // Collect LFO modifiers only from devices on the destination track
            // that are actually sidechained from this source track.
            for (const auto& element : otherTrack.chain.fxChainElements) {
                if (!isDevice(element))
                    continue;
                const auto& device = getDevice(element);
                // Only collect from devices whose sidechain source is this track
                if (device.sidechain.sourceTrackId != track.id)
                    continue;
                collectDeviceLFOs(device, otherTrack.id);
            }
            rackSyncManager_.collectLFOModifiersWithModesForSidechainSource(otherTrack.id, track.id,
                                                                            lfos, modes);
        }

        // Write to cache entry (capped at kMaxLFOs)
        // Self-track LFOs come first (indices 0..selfTrackCount-1),
        // cross-track LFOs follow (indices selfTrackCount..count-1).
        entry.hasAudioTrigger = std::any_of(modes.begin(), modes.end(), [](LFOTriggerMode mode) {
            return mode == LFOTriggerMode::Audio;
        });

        // Followers key off this track's post-FX audio (FollowerSourceTapPlugin),
        // independent of the pre-FX trigger monitor above. Copy each follower's
        // detector config; the IIRFilter state stays default (fresh) here.
        entry.followerCount =
            std::min(static_cast<int>(followerCollect.size()), PerTrackEntry::kMaxFollowers);
        for (int i = 0; i < entry.followerCount; ++i) {
            auto& slot = entry.followers[static_cast<size_t>(i)];
            slot.mod = followerCollect[static_cast<size_t>(i)].first;
            const ModInfo* mi = followerCollect[static_cast<size_t>(i)].second;
            slot.hpEnabled = mi->followerHpEnabled;
            slot.lpEnabled = mi->followerLpEnabled;
            slot.gain = juce::Decibels::decibelsToGain(mi->followerGainDb);
            slot.hpFreq = mi->followerHpFreq;
            slot.lpFreq = mi->followerLpFreq;
            MAGDA_ADSR_AUDIO_LOG(
                "follower-cache slot sourceTrack="
                << track.id << " slot=" << i << " modId=" << static_cast<int>(mi->id)
                << " gainDb=" << mi->followerGainDb << " gain=" << slot.gain
                << " hpOn=" << static_cast<int>(slot.hpEnabled) << " hpHz=" << slot.hpFreq
                << " lpOn=" << static_cast<int>(slot.lpEnabled) << " lpHz=" << slot.lpFreq);
        }
        entry.hasFollowerSource = entry.followerCount > 0;
        if (entry.hasFollowerSource) {
            MAGDA_ADSR_AUDIO_LOG("follower-cache entry sourceTrack="
                                 << track.id << " followerCount=" << entry.followerCount
                                 << " audioTrigger=" << static_cast<int>(entry.hasAudioTrigger));
        }
        entry.count = std::min(static_cast<int>(lfos.size()), PerTrackEntry::kMaxMods);
        if (entry.count > 0) {
            MAGDA_ADSR_AUDIO_LOG("cache entry sourceTrack=" << track.id << " count=" << entry.count
                                                            << " selfCount=" << selfTrackCount
                                                            << " totalCollected="
                                                            << static_cast<int>(lfos.size()));
        }
        for (int i = 0; i < entry.count; ++i) {
            entry.mods[static_cast<size_t>(i)] = lfos[static_cast<size_t>(i)];
            entry.isCrossTrack[static_cast<size_t>(i)] = (i >= selfTrackCount);
            entry.trigMode[static_cast<size_t>(i)] = (static_cast<size_t>(i) < modes.size())
                                                         ? modes[static_cast<size_t>(i)]
                                                         : LFOTriggerMode::Free;
            MAGDA_ADSR_AUDIO_LOG(
                "cache slot sourceTrack="
                << track.id << " idx=" << i << " type="
                << (dynamic_cast<te::ADSRModifier*>(entry.mods[static_cast<size_t>(i)]) ? "ADSR"
                                                                                        : "LFO")
                << " crossTrack=" << static_cast<int>(entry.isCrossTrack[static_cast<size_t>(i)])
                << " mode=" << static_cast<int>(entry.trigMode[static_cast<size_t>(i)]));
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
    for (const auto& element : trackInfo->chain.fxChainElements) {
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
                auto it = findSyncedDevice(devicePath);
                if (it == syncedDevices_.end())
                    return nullptr;
                DeviceInfo* device = tm.getDeviceInChainByPath(devicePath);
                if (!device)
                    device = tm.getDevice(it->second.trackId, devicePath.getDeviceId());
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
