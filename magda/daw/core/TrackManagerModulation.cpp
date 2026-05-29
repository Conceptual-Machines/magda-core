#include <cmath>
#include <set>

#include "../audio/plugins/SidechainTriggerBus.hpp"
#include "ModulatorEngine.hpp"
#include "RackInfo.hpp"
#include "SidechainTraversal.hpp"
#include "TrackManager.hpp"

namespace magda {

namespace {

struct ModTickInputs {
    bool midiTriggered = false;
    bool midiNoteOff = false;
    float audioPeakLevel = 0.0f;
    double deltaTime = 0.0;
    double bpm = 120.0;
    bool transportPlaying = false;
    bool transportJustStarted = false;
    bool transportJustLooped = false;
    bool transportJustStopped = false;
};

bool computeTriggerRequest(ModInfo& mod, const ModTickInputs& in) {
    switch (mod.triggerMode) {
        case LFOTriggerMode::Free:
            return false;
        case LFOTriggerMode::Transport:
            return in.transportJustStarted || in.transportJustLooped;
        case LFOTriggerMode::MIDI:
            return in.midiTriggered;
        case LFOTriggerMode::Audio: {
            float attackCoeff = 1.0f;
            float releaseCoeff = 1.0f;
            if (mod.audioAttackMs > 0.0f)
                attackCoeff = 1.0f - std::exp(-static_cast<float>(in.deltaTime) /
                                              (mod.audioAttackMs * 0.001f));
            if (mod.audioReleaseMs > 0.0f)
                releaseCoeff = 1.0f - std::exp(-static_cast<float>(in.deltaTime) /
                                               (mod.audioReleaseMs * 0.001f));

            if (in.audioPeakLevel > mod.audioEnvLevel)
                mod.audioEnvLevel += attackCoeff * (in.audioPeakLevel - mod.audioEnvLevel);
            else
                mod.audioEnvLevel += releaseCoeff * (in.audioPeakLevel - mod.audioEnvLevel);

            constexpr float threshold = 0.1f;
            if (!mod.audioGateOpen && in.audioPeakLevel > threshold) {
                mod.audioGateOpen = true;
                return true;
            }
            if (mod.audioGateOpen && in.audioPeakLevel < threshold)
                mod.audioGateOpen = false;
            return false;
        }
    }
    return false;
}

void rearmOneShotIfNeeded(ModInfo& mod, bool triggerRequested) {
    if (!mod.oneShot || !mod.oneShotComplete)
        return;

    if (mod.triggerMode == LFOTriggerMode::MIDI && triggerRequested)
        mod.oneShotComplete = false;
    else if (mod.triggerMode == LFOTriggerMode::Audio && !mod.audioGateOpen)
        mod.oneShotComplete = false;
}

bool canRetrigger(const ModInfo& mod) {
    if (!mod.oneShot)
        return true;

    switch (mod.triggerMode) {
        case LFOTriggerMode::MIDI:
        case LFOTriggerMode::Transport:
            return true;
        case LFOTriggerMode::Audio:
            // Audio one-shot: gate bounces mid-cycle should not retrigger.
            // Re-arm happens in rearmOneShotIfNeeded once the cycle completes
            // and the gate closes; a fresh trigger then fires via the !running
            // path in shouldApplyTrigger.
            return false;
        case LFOTriggerMode::Free:
            return false;
    }
    return false;
}

bool shouldApplyTrigger(const ModInfo& mod, bool triggerRequested) {
    if (!triggerRequested || mod.oneShotComplete)
        return false;
    if (!mod.running)
        return true;
    return canRetrigger(mod);
}

bool shouldStopRunning(const ModInfo& mod, const ModTickInputs& in) {
    if (!mod.running)
        return false;
    if (mod.triggerMode == LFOTriggerMode::Transport && in.transportJustStopped)
        return true;
    if (mod.oneShot)
        return false;
    if (mod.triggerMode == LFOTriggerMode::MIDI && in.midiNoteOff)
        return true;
    if (mod.triggerMode == LFOTriggerMode::Audio && !mod.audioGateOpen)
        return true;
    return false;
}

}  // namespace

// ============================================================================
// Unified ChainNode-based modulation API (issue #1131 step 1)
// ============================================================================
//
// Path → ChainNode resolver. The TrackManager owns the canonical tree
// (TrackInfo with chainElements, racks, nested chains, devices), and a
// ChainNodePath identifies a single node within it. resolveChainNode walks
// the path once and hands back a non-owning view onto that node's macros /
// mods (and parameters, for devices). Reuses getRackByPath /
// getDeviceInChainByPath so the traversal logic stays in one place.

ChainNode TrackManager::resolveChainNode(const ChainNodePath& path) {
    ChainNode node;
    node.trackId = path.trackId;

    switch (path.getType()) {
        case ChainNodeType::Track: {
            auto* track = getTrack(path.trackId);
            if (!track)
                return ChainNode{};
            node.scope = ChainScope::Track;
            node.macros = &track->macros;
            node.mods = &track->mods;
            return node;
        }
        case ChainNodeType::Rack: {
            auto* rack = getRackByPath(path);
            if (!rack)
                return ChainNode{};
            node.scope = ChainScope::Rack;
            node.rackId = rack->id;
            node.macros = &rack->macros;
            node.mods = &rack->mods;
            return node;
        }
        case ChainNodeType::Device:
        case ChainNodeType::TopLevelDevice: {
            auto* device = getDeviceInChainByPath(path);
            if (!device)
                return ChainNode{};
            node.scope = ChainScope::Device;
            node.deviceId = device->id;
            node.params = &device->parameters;
            if (path.isPostFx())
                return node;
            node.macros = &device->macros;
            node.mods = &device->mods;
            return node;
        }
        case ChainNodeType::Chain:
        case ChainNodeType::None:
            break;
    }
    return ChainNode{};
}

ConstChainNode TrackManager::resolveChainNode(const ChainNodePath& path) const {
    ConstChainNode node;
    node.trackId = path.trackId;

    switch (path.getType()) {
        case ChainNodeType::Track: {
            const auto* track = getTrack(path.trackId);
            if (!track)
                return ConstChainNode{};
            node.scope = ChainScope::Track;
            node.macros = &track->macros;
            node.mods = &track->mods;
            return node;
        }
        case ChainNodeType::Rack: {
            const auto* rack = getRackByPath(path);
            if (!rack)
                return ConstChainNode{};
            node.scope = ChainScope::Rack;
            node.rackId = rack->id;
            node.macros = &rack->macros;
            node.mods = &rack->mods;
            return node;
        }
        case ChainNodeType::Device:
        case ChainNodeType::TopLevelDevice: {
            const auto* device = getDeviceInChainByPath(path);
            if (!device)
                return ConstChainNode{};
            node.scope = ChainScope::Device;
            node.deviceId = device->id;
            node.params = &device->parameters;
            if (path.isPostFx())
                return node;
            node.macros = &device->macros;
            node.mods = &device->mods;
            return node;
        }
        case ChainNodeType::Chain:
        case ChainNodeType::None:
            break;
    }
    return ConstChainNode{};
}

namespace {

// True for paths whose macro/mod index is in range. Centralises the bounds
// check so every unified op below can early-return uniformly.
template <typename Array> bool indexInRange(const Array* arr, int idx) {
    return arr != nullptr && idx >= 0 && idx < static_cast<int>(arr->size());
}

}  // namespace

// ----------------------------------------------------------------------------
// Unified Macro ops
// ----------------------------------------------------------------------------

void TrackManager::setMacroValue(const ChainNodePath& path, int macroIndex, float value) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    float clampedValue = juce::jlimit(0.0f, 1.0f, value);
    auto& macro = (*node.macros)[macroIndex];
    if (std::abs(macro.value - clampedValue) <= 1.0e-6f)
        return;

    macro.value = clampedValue;
    notifyMacroValueChanged(path.trackId, node.scope, node.notifyId(), macroIndex, clampedValue);
}

void TrackManager::setMacroTarget(const ChainNodePath& path, int macroIndex, ControlTarget target) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    auto& macro = (*node.macros)[macroIndex];

    if (!macro.getLink(target)) {
        MacroLink newLink;
        newLink.target = target;
        // ModParam picks come from a menu (no drag overlay); 100% so the link
        // is immediately audible. Knob-target picks default to 30% to leave
        // headroom for the overlay drag.
        newLink.amount = target.kind == ControlTarget::Kind::ModParam ? 1.0f : 0.3f;
        newLink.bipolar = false;
        macro.links.push_back(newLink);
        notifyDeviceModifiersChanged(path.trackId);
    }
}

void TrackManager::setMacroLinkAmount(const ChainNodePath& path, int macroIndex,
                                      ControlTarget target, float amount) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    auto& macro = (*node.macros)[macroIndex];

    bool created = false;
    if (auto* link = macro.getLink(target)) {
        link->amount = amount;
    } else {
        MacroLink newLink;
        newLink.target = target;
        newLink.amount = amount;
        macro.links.push_back(newLink);
        created = true;
    }
    // ModParam links don't change device topology — keep the lighter
    // notification so an open mod editor isn't torn down. Newly-created
    // DeviceParam links need TE modifier assignment, which trackDevices
    // covers.
    if (created && target.kind != ControlTarget::Kind::ModParam) {
        notifyTrackDevicesChanged(path.trackId);
    } else {
        notifyDeviceModifiersChanged(path.trackId);
    }
}

void TrackManager::setMacroLinkBipolar(const ChainNodePath& path, int macroIndex,
                                       ControlTarget target, bool bipolar) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    if (auto* link = (*node.macros)[macroIndex].getLink(target)) {
        link->bipolar = bipolar;
        notifyDeviceModifiersChanged(path.trackId);
    }
}

void TrackManager::setMacroName(const ChainNodePath& path, int macroIndex,
                                const juce::String& name) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    (*node.macros)[macroIndex].name = name;
    // Don't notify - rename doesn't need UI rebuild
}

void TrackManager::removeMacroLink(const ChainNodePath& path, int macroIndex,
                                   ControlTarget target) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    (*node.macros)[macroIndex].removeLink(target);
    // Lighter notify across all scopes — modifier sync alone is sufficient to
    // drop the TE assignment, no full device-topology rebuild needed.
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::clearAllMacroLinks(const ChainNodePath& path, int macroIndex) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.macros, macroIndex))
        return;
    auto& macro = (*node.macros)[macroIndex];
    macro.links.clear();
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::addMacroPage(const ChainNodePath& path) {
    auto node = resolveChainNode(path);
    if (!node.valid())
        return;
    magda::addMacroPage(*node.macros);
    notifyTrackDevicesChanged(path.trackId);
}

void TrackManager::removeMacroPage(const ChainNodePath& path) {
    auto node = resolveChainNode(path);
    if (!node.valid())
        return;
    if (magda::removeMacroPage(*node.macros))
        notifyTrackDevicesChanged(path.trackId);
}

// ----------------------------------------------------------------------------
// Unified Mod ops
// ----------------------------------------------------------------------------

void TrackManager::addMod(const ChainNodePath& path, int slotIndex, ModType type,
                          LFOWaveform waveform) {
    auto node = resolveChainNode(path);
    if (!node.valid())
        return;
    auto& mods = *node.mods;
    if (slotIndex < 0 || slotIndex > static_cast<int>(mods.size()))
        return;
    ModInfo newMod(slotIndex);
    newMod.type = type;
    newMod.waveform = waveform;
    if (waveform == LFOWaveform::Custom) {
        newMod.name = "Curve " + juce::String(slotIndex + 1);
    } else {
        newMod.name = ModInfo::getDefaultName(slotIndex, type);
    }
    mods.insert(mods.begin() + slotIndex, newMod);
    for (int i = slotIndex + 1; i < static_cast<int>(mods.size()); ++i) {
        mods[i].id = i;
    }
    // Caller handles UI update — notifying here would close the open panel.
}

void TrackManager::removeMod(const ChainNodePath& path, int modIndex) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    auto& mods = *node.mods;
    mods.erase(mods.begin() + modIndex);
    for (int i = modIndex; i < static_cast<int>(mods.size()); ++i) {
        mods[i].id = i;
        mods[i].name = ModInfo::getDefaultName(i, mods[i].type);
    }
    // Async notify so the caller can unwind before rebuild.
    auto trackId = path.trackId;
    juce::MessageManager::callAsync([trackId]() {
        if (juce::JUCEApplicationBase::getInstance() == nullptr)
            return;
        TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
    });
}

void TrackManager::setModTarget(const ChainNodePath& path, int modIndex, ControlTarget target) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    auto& mod = (*node.mods)[modIndex];

    if (target.isValid()) {
        const float defaultAmount = target.kind == ControlTarget::Kind::ModParam ? 1.0f : 0.0f;
        mod.addLink(target, defaultAmount);
    }
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModLinkAmount(const ChainNodePath& path, int modIndex, ControlTarget target,
                                    float amount) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    auto& mod = (*node.mods)[modIndex];
    if (auto* link = mod.getLink(target)) {
        link->amount = amount;
    } else {
        mod.links.push_back({target, amount});
    }
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModLinkBipolar(const ChainNodePath& path, int modIndex, ControlTarget target,
                                     bool bipolar) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    if (auto* link = (*node.mods)[modIndex].getLink(target)) {
        link->bipolar = bipolar;
        notifyDeviceModifiersChanged(path.trackId);
    }
}

void TrackManager::setModLinkEnabled(const ChainNodePath& path, int modIndex, ControlTarget target,
                                     bool enabled) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    if (auto* link = (*node.mods)[modIndex].getLink(target)) {
        link->enabled = enabled;
        notifyDeviceModifiersChanged(path.trackId);
    }
}

void TrackManager::setModName(const ChainNodePath& path, int modIndex, const juce::String& name) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].name = name;
}

void TrackManager::setModType(const ChainNodePath& path, int modIndex, ModType type) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    auto& mod = (*node.mods)[modIndex];
    auto oldType = mod.type;
    mod.type = type;
    auto defaultOldName = ModInfo::getDefaultName(modIndex, oldType);
    if (mod.name == defaultOldName) {
        mod.name = ModInfo::getDefaultName(modIndex, type);
    }
    notifyTrackDevicesChanged(path.trackId);
}

void TrackManager::setModWaveform(const ChainNodePath& path, int modIndex, LFOWaveform waveform) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].waveform = waveform;
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModRate(const ChainNodePath& path, int modIndex, float rate) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    auto& mod = (*node.mods)[modIndex];
    mod.rate = rate;
    notifyDeviceModifiersChanged(path.trackId);
    notifyModParameterChanged(path.trackId, path, mod.id, /*paramIndex=*/0, rate);
}

void TrackManager::setModPhaseOffset(const ChainNodePath& path, int modIndex, float phaseOffset) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModTempoSync(const ChainNodePath& path, int modIndex, bool tempoSync) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].tempoSync = tempoSync;
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModSyncDivision(const ChainNodePath& path, int modIndex,
                                      SyncDivision division) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    auto& mod = (*node.mods)[modIndex];
    mod.syncDivision = division;
    notifyDeviceModifiersChanged(path.trackId);
    notifyModParameterChanged(path.trackId, path, mod.id, /*paramIndex=*/1,
                              static_cast<float>(syncDivisionToTeRateOrdinal(division)));
}

void TrackManager::setModTriggerMode(const ChainNodePath& path, int modIndex, LFOTriggerMode mode) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].triggerMode = mode;
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModCurvePreset(const ChainNodePath& path, int modIndex, CurvePreset preset) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].curvePreset = preset;
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::notifyModCurveChanged(const ChainNodePath& path) {
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModAudioAttack(const ChainNodePath& path, int modIndex, float ms) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].audioAttackMs = juce::jlimit(0.1f, 500.0f, ms);
}

void TrackManager::setModAudioRelease(const ChainNodePath& path, int modIndex, float ms) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].audioReleaseMs = juce::jlimit(1.0f, 2000.0f, ms);
}

void TrackManager::removeModLink(const ChainNodePath& path, int modIndex, ControlTarget target) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].removeLink(target);
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::clearAllModLinks(const ChainNodePath& path, int modIndex) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].links.clear();
    notifyDeviceModifiersChanged(path.trackId);
}

void TrackManager::setModEnabled(const ChainNodePath& path, int modIndex, bool enabled) {
    auto node = resolveChainNode(path);
    if (!indexInRange(node.mods, modIndex))
        return;
    (*node.mods)[modIndex].enabled = enabled;
    notifyTrackDevicesChanged(path.trackId);
}

void TrackManager::addModPage(const ChainNodePath& path) {
    auto node = resolveChainNode(path);
    if (!node.valid())
        return;
    magda::addModPage(*node.mods);
    notifyTrackDevicesChanged(path.trackId);
}

void TrackManager::removeModPage(const ChainNodePath& path) {
    auto node = resolveChainNode(path);
    if (!node.valid())
        return;
    if (magda::removeModPage(*node.mods))
        notifyTrackDevicesChanged(path.trackId);
}

void TrackManager::triggerMidiNoteOn(TrackId trackId) {
    std::lock_guard<std::mutex> lock(midiTriggerMutex_);
    pendingMidiNoteOns_[trackId]++;
}

const ModInfo* TrackManager::getModById(TrackId trackId, ModId modId) const {
    const auto* track = getTrack(trackId);
    if (!track)
        return nullptr;
    // Search track-level mods first
    for (const auto& mod : track->mods) {
        if (mod.id == modId)
            return &mod;
    }
    for (const auto& element : track->chain.fxChainElements) {
        if (std::holds_alternative<DeviceInfo>(element)) {
            for (const auto& mod : std::get<DeviceInfo>(element).mods) {
                if (mod.id == modId)
                    return &mod;
            }
        } else if (std::holds_alternative<std::unique_ptr<RackInfo>>(element)) {
            const auto& rackPtr = std::get<std::unique_ptr<RackInfo>>(element);
            if (rackPtr) {
                for (const auto& mod : rackPtr->mods) {
                    if (mod.id == modId)
                        return &mod;
                }
            }
        }
    }
    return nullptr;
}

void TrackManager::triggerMidiNoteOff(TrackId trackId) {
    std::lock_guard<std::mutex> lock(midiTriggerMutex_);
    pendingMidiNoteOffs_[trackId]++;
}

TrackManager::TransportSnapshot TrackManager::consumeTransportState() {
    return {transportBpm_.load(std::memory_order_acquire),
            transportPlaying_.load(std::memory_order_acquire),
            transportJustStarted_.exchange(false, std::memory_order_acq_rel),
            transportJustLooped_.exchange(false, std::memory_order_acq_rel),
            transportJustStopped_.exchange(false, std::memory_order_acq_rel)};
}

void TrackManager::updateTransportState(bool playing, double bpm, bool justStarted,
                                        bool justLooped) {
    bool wasPlaying = transportPlaying_.exchange(playing, std::memory_order_acq_rel);
    transportBpm_.store(bpm, std::memory_order_release);
    if (justStarted)
        transportJustStarted_.store(true, std::memory_order_release);
    if (justLooped)
        transportJustLooped_.store(true, std::memory_order_release);
    if (wasPlaying && !playing)
        transportJustStopped_.store(true, std::memory_order_release);
}

// ============================================================================
// Mod Updates
// ============================================================================

void TrackManager::updateAllMods(double deltaTime, double bpm, bool transportJustStarted,
                                 bool transportJustLooped, bool transportJustStopped,
                                 bool transportPlaying) {
    // Snapshot MIDI trigger counts (thread-safe)
    std::map<TrackId, int> noteOnsThisTick;
    std::map<TrackId, int> noteOffsThisTick;
    {
        std::lock_guard<std::mutex> lock(midiTriggerMutex_);
        noteOnsThisTick.swap(pendingMidiNoteOns_);
        noteOffsThisTick.swap(pendingMidiNoteOffs_);
    }

    // Read audio-thread sidechain triggers from the lock-free bus.
    // These are kept separate from external MIDI — they only feed into
    // sidechain-sourced devices, not into the track's own MIDI trigger signal.
    auto& bus = SidechainTriggerBus::getInstance();
    std::array<float, kMaxBusTracks> audioPeakLevels{};
    std::map<TrackId, int> busNoteOnsThisTick;
    std::map<TrackId, int> busNoteOffsThisTick;
    for (const auto& track : tracks_) {
        if (track.id < 0 || track.id >= kMaxBusTracks)
            continue;
        uint64_t currentNoteOn = bus.getNoteOnCounter(track.id);
        uint64_t currentNoteOff = bus.getNoteOffCounter(track.id);
        int busNewNoteOns = static_cast<int>(currentNoteOn - lastBusNoteOn_[track.id]);
        int busNewNoteOffs = static_cast<int>(currentNoteOff - lastBusNoteOff_[track.id]);
        lastBusNoteOn_[track.id] = currentNoteOn;
        lastBusNoteOff_[track.id] = currentNoteOff;
        if (busNewNoteOns > 0)
            busNoteOnsThisTick[track.id] = busNewNoteOns;
        if (busNewNoteOffs > 0)
            busNoteOffsThisTick[track.id] = busNewNoteOffs;
        audioPeakLevels[track.id] = bus.getAudioPeakLevel(track.id);
    }

    // Compute per-track MIDI trigger signals for LFOs.
    // Both external MIDI (pendingMidiNoteOns_) and internal MIDI (SidechainTriggerBus)
    // feed into note-on retriggering. For sidechain MIDI, note-off is intentionally
    // ignored: external sidechain LFOs are note-on-only retriggers, not gated notes.
    // Held-note tracking remains only for direct external MIDI on the destination track.
    std::set<TrackId> midiNoteOnTracks;
    std::set<TrackId> midiAllNotesOffTracks;
    {
        // External MIDI trigger signals
        for (const auto& [id, count] : noteOnsThisTick) {
            if (count > 0)
                midiNoteOnTracks.insert(id);
        }

        // Internal (bus) MIDI trigger signals
        for (const auto& [id, count] : busNoteOnsThisTick) {
            if (count > 0)
                midiNoteOnTracks.insert(id);
        }

        // Track held-note state for gate-close detection (external MIDI only,
        // because bus note-off counts can be imprecise and would corrupt the
        // held count, preventing gate-close from ever firing)
        std::set<TrackId> activeTracks;
        for (const auto& [id, _] : noteOnsThisTick)
            activeTracks.insert(id);
        for (const auto& [id, _] : noteOffsThisTick)
            activeTracks.insert(id);

        for (auto trackId : activeTracks) {
            int prevHeld = midiHeldNotes_[trackId];
            int ons = noteOnsThisTick.count(trackId) ? noteOnsThisTick[trackId] : 0;
            int offs = noteOffsThisTick.count(trackId) ? noteOffsThisTick[trackId] : 0;
            int newHeld = std::max(0, prevHeld + ons - offs);
            midiHeldNotes_[trackId] = newHeld;

            if (prevHeld > 0 && newHeld == 0)
                midiAllNotesOffTracks.insert(trackId);
        }

        // Intentionally ignore bus note-offs. Cross-track MIDI sidechain is
        // note-on-only retriggering, so stopping on sidechain note-off creates
        // ambiguous edge cases when off/on land in the same mod-timer tick.
    }

    // Lambda to update a single mod's phase and value.
    // Returns true if 'running' state changed (needs TE assignment sync).
    // scopeMacros / scopeMods are the SAME-scope macros / mods that may
    // target this mod's rate via a ModParam-kind link — used to compute the
    // effective rate so the UI animation matches what the audio LFO does.
    auto updateMod =
        [deltaTime, bpm, transportJustStarted, transportJustLooped, transportJustStopped,
         transportPlaying](ModInfo& mod, bool midiTriggered, bool midiNoteOff, float audioPeakLevel,
                           const std::vector<MacroInfo>& scopeMacros,
                           const std::vector<ModInfo>& scopeMods) -> bool {
        bool wasRunning = mod.running;
        ModTickInputs inputs{
            midiTriggered,    midiNoteOff,          audioPeakLevel,      deltaTime,           bpm,
            transportPlaying, transportJustStarted, transportJustLooped, transportJustStopped};
        // Skip disabled mods - set value to 0 so they don't affect modulation
        if (!mod.enabled) {
            mod.value = 0.0f;
            mod.triggered = false;
            return false;
        }

        if (mod.type == ModType::LFO) {
            bool triggerRequested = computeTriggerRequest(mod, inputs);
            rearmOneShotIfNeeded(mod, triggerRequested);
            bool shouldTrigger = shouldApplyTrigger(mod, triggerRequested);

            if (shouldTrigger) {
                mod.phase = 0.0f;
                mod.triggered = true;
                mod.triggerCount++;
                mod.running = true;
            } else {
                mod.triggered = false;
            }

            if (shouldStopRunning(mod, inputs))
                mod.running = false;

            if (mod.triggerMode == LFOTriggerMode::Transport && inputs.transportPlaying &&
                !inputs.transportJustStopped && !mod.running) {
                mod.running = true;
            }

            if (mod.triggerMode == LFOTriggerMode::Transport && transportJustStopped &&
                !mod.running) {
                mod.running = false;
                mod.phase = 0.0f;
            }

            // Gate: only advance phase for Free mode, or when running for triggered modes
            bool shouldAdvance = (mod.triggerMode == LFOTriggerMode::Free) || mod.running;

            if (shouldAdvance) {
                // Calculate effective rate (Hz or tempo-synced)
                float effectiveRate = mod.rate;
                if (mod.tempoSync) {
                    effectiveRate = ModulatorEngine::calculateSyncRateHz(mod.syncDivision, bpm);
                }

                // Apply incoming ModParam-kind modulation from same-scope
                // macros / mods so the UI animation matches what the audio
                // LFO actually does. TE drives the audio side via its own
                // modifier graph; here we mirror it on MAGDA's parallel
                // visual sim by summing each link's offset * amount and
                // applying it as a multiplicative shift on the rate (the
                // rate slider is logarithmic 0.05..20 Hz, so a normalized
                // unit covers ~8.6 octaves — multiplying by 20/0.05 to the
                // power of total matches the slider's perceptual mapping
                // and what the user hears).
                float modTotal = 0.0f;
                for (const auto& m : scopeMacros) {
                    for (const auto& l : m.links) {
                        if (l.target.kind != ControlTarget::Kind::ModParam ||
                            l.target.modId != mod.id || l.target.modParamIndex != 0)
                            continue;
                        float offset = l.bipolar ? (m.value * 2.0f - 1.0f) : m.value;
                        modTotal += offset * l.amount;
                    }
                }
                for (const auto& m : scopeMods) {
                    if (m.id == mod.id)
                        continue;
                    for (const auto& l : m.links) {
                        if (!l.enabled || l.target.kind != ControlTarget::Kind::ModParam ||
                            l.target.modId != mod.id || l.target.modParamIndex != 0)
                            continue;
                        float offset = l.bipolar ? (m.value * 2.0f - 1.0f) : m.value;
                        modTotal += offset * l.amount;
                    }
                }
                if (modTotal != 0.0f) {
                    // 20 / 0.05 = 400 = ~8.64 octaves over normalized [-1, 1]
                    constexpr float kRateRangeRatio = 20.0f / 0.05f;
                    effectiveRate *= std::pow(kRateRangeRatio, modTotal);
                    effectiveRate = juce::jlimit(0.05f, 20.0f, effectiveRate);
                }

                // Update phase
                mod.phase += static_cast<float>(effectiveRate * deltaTime);
                if (mod.oneShot) {
                    // One-shot: clamp at end of cycle, hold final value
                    if (mod.phase >= 1.0f) {
                        mod.phase = 1.0f;
                        mod.running = false;
                        mod.oneShotComplete = true;
                    }
                } else {
                    // Loop: wrap at 1.0
                    while (mod.phase >= 1.0f)
                        mod.phase -= 1.0f;
                }
                // Apply phase offset when generating waveform
                if (mod.oneShot && mod.phase >= 1.0f) {
                    // One-shot end: return last point value directly to avoid
                    // wrap-around interpolation back toward the first point
                    mod.value = ModulatorEngine::generateOneShotEndValue(mod);
                } else {
                    float effectivePhase = std::fmod(mod.phase + mod.phaseOffset, 1.0f);
                    mod.value = ModulatorEngine::generateWaveformForMod(mod, effectivePhase);
                }
            } else {
                // Not running: one-shot holds end value, otherwise no output
                if (mod.oneShot)
                    mod.value = ModulatorEngine::generateOneShotEndValue(mod);
                else
                    mod.value = 0.0f;
            }
        }
        return mod.running != wasRunning;
    };

    // Audio sidechain triggering of TE LFOs is now handled on the audio thread
    // by AudioSidechainMonitorPlugin — no message-thread retrigger needed.

    // Recursive lambda to update mods in chain elements
    // Returns true if any mod's running state changed
    std::function<bool(ChainElement&, bool, bool, float, TrackId)> updateElementMods =
        [&](ChainElement& element, bool midiTriggered, bool midiNoteOff, float audioPeak,
            TrackId ownerTrackId) -> bool {
        bool changed = false;
        if (isDevice(element)) {
            DeviceInfo& device = magda::getDevice(element);
            bool deviceMidiTriggered = midiTriggered;
            bool deviceMidiNoteOff = midiNoteOff;
            float deviceAudioPeak = audioPeak;

            // Cross-track sidechain: replace self triggers with source track's
            if (device.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                auto srcId = device.sidechain.sourceTrackId;
                // Replace self triggers with source track's MIDI triggers
                deviceMidiTriggered = midiNoteOnTracks.count(srcId) > 0;
                deviceMidiNoteOff = midiAllNotesOffTracks.count(srcId) > 0;

                // Audio peak from source track (for UI envelope tracking)
                if (srcId >= 0 && srcId < kMaxBusTracks)
                    deviceAudioPeak = audioPeakLevels[srcId];
            }

            for (auto& mod : device.mods) {
                changed |= updateMod(mod, deviceMidiTriggered, deviceMidiNoteOff, deviceAudioPeak,
                                     device.macros, device.mods);
            }
        } else if (isRack(element)) {
            RackInfo& rack = magda::getRack(element);
            bool rackMidiTriggered = midiTriggered;
            bool rackMidiNoteOff = midiNoteOff;
            float rackAudioPeak = audioPeak;

            // A rack-level mod can inherit the first explicit sidechain source
            // from nested rack/device contents when the rack itself has no
            // source. Keep this recursive so nested racks behave like flat racks.
            if (auto sourceTrackId = sidechain::findFirstSource(rack)) {
                auto srcId = *sourceTrackId;
                rackMidiTriggered = midiNoteOnTracks.count(srcId) > 0;
                rackMidiNoteOff = midiAllNotesOffTracks.count(srcId) > 0;
                if (srcId >= 0 && srcId < kMaxBusTracks)
                    rackAudioPeak = audioPeakLevels[srcId];
            }

            for (auto& mod : rack.mods) {
                changed |= updateMod(mod, rackMidiTriggered, rackMidiNoteOff, rackAudioPeak,
                                     rack.macros, rack.mods);
            }
            for (auto& chain : rack.chains) {
                for (auto& chainElement : chain.elements) {
                    changed |= updateElementMods(chainElement, rackMidiTriggered, rackMidiNoteOff,
                                                 rackAudioPeak, ownerTrackId);
                }
            }
        }
        return changed;
    };

    // Update mods in all tracks, collect those needing TE assignment sync
    std::vector<TrackId> tracksNeedingSync;
    for (auto& track : tracks_) {
        if (track.id < 0 || track.id >= kMaxBusTracks)
            continue;
        bool trackMidiTriggered = midiNoteOnTracks.count(track.id) > 0;
        bool trackMidiNoteOff = midiAllNotesOffTracks.count(track.id) > 0;
        float trackAudioPeak = audioPeakLevels[track.id];
        bool trackChanged = false;

        // Track-level mods (global scope)
        for (auto& mod : track.mods) {
            trackChanged |= updateMod(mod, trackMidiTriggered, trackMidiNoteOff, trackAudioPeak,
                                      track.macros, track.mods);
        }

        // Device/rack-level mods
        for (auto& element : track.chain.fxChainElements) {
            trackChanged |= updateElementMods(element, trackMidiTriggered, trackMidiNoteOff,
                                              trackAudioPeak, track.id);
        }
        if (trackChanged)
            tracksNeedingSync.push_back(track.id);
    }

    // Notify TE to sync assignment values for tracks where running state changed
    for (auto trackId : tracksNeedingSync) {
        notifyDeviceModifiersChanged(trackId);
    }
}

}  // namespace magda
