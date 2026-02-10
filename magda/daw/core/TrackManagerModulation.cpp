#include <cmath>

#include "../audio/SidechainTriggerBus.hpp"
#include "ModulatorEngine.hpp"
#include "RackInfo.hpp"
#include "TrackManager.hpp"

namespace magda {

// ============================================================================
// Rack Macro Management
// ============================================================================

void TrackManager::setRackMacroValue(const ChainNodePath& rackPath, int macroIndex, float value) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        float clampedValue = juce::jlimit(0.0f, 1.0f, value);
        rack->macros[macroIndex].value = clampedValue;
        notifyMacroValueChanged(rackPath.trackId, true, rack->id, macroIndex, clampedValue);
    }
}

void TrackManager::setRackMacroTarget(const ChainNodePath& rackPath, int macroIndex,
                                      MacroTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        rack->macros[macroIndex].target = target;
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackMacroName(const ChainNodePath& rackPath, int macroIndex,
                                    const juce::String& name) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        rack->macros[macroIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackMacroLinkAmount(const ChainNodePath& rackPath, int macroIndex,
                                          MacroTarget target, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        bool created = false;
        if (auto* link = rack->macros[macroIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            rack->macros[macroIndex].links.push_back(newLink);
            created = true;
        }
        // Notify when a new link is created (needs TE modifier assignment)
        if (created) {
            notifyTrackDevicesChanged(rackPath.trackId);
        } else {
            // Existing link amount changed — resync TE assignments
            notifyDeviceModifiersChanged(rackPath.trackId);
        }
    }
}

void TrackManager::addRackMacroPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        addMacroPage(rack->macros);
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::removeRackMacroPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (removeMacroPage(rack->macros)) {
            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

// ============================================================================
// Rack Mod Management
// ============================================================================

void TrackManager::setRackModAmount(const ChainNodePath& rackPath, int modIndex, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].amount = juce::jlimit(0.0f, 1.0f, amount);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModTarget(const ChainNodePath& rackPath, int modIndex, ModTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].target = target;
        // Use modifier-only notify to avoid full UI rebuild (panel stays open)
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModLinkAmount(const ChainNodePath& rackPath, int modIndex,
                                        ModTarget target, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        if (auto* link = rack->mods[modIndex].getLink(target)) {
            link->amount = amount;
        } else {
            ModLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            rack->mods[modIndex].links.push_back(newLink);
        }
        // Also update legacy amount if target matches
        if (rack->mods[modIndex].target == target) {
            rack->mods[modIndex].amount = amount;
        }
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModName(const ChainNodePath& rackPath, int modIndex,
                                  const juce::String& name) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModType(const ChainNodePath& rackPath, int modIndex, ModType type) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].type = type;
        // Update name to default for new type if it was default
        auto defaultOldName = ModInfo::getDefaultName(modIndex, rack->mods[modIndex].type);
        if (rack->mods[modIndex].name == defaultOldName) {
            rack->mods[modIndex].name = ModInfo::getDefaultName(modIndex, type);
        }
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModWaveform(const ChainNodePath& rackPath, int modIndex,
                                      LFOWaveform waveform) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].waveform = waveform;
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModRate(const ChainNodePath& rackPath, int modIndex, float rate) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].rate = rate;
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModPhaseOffset(const ChainNodePath& rackPath, int modIndex,
                                         float phaseOffset) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModTempoSync(const ChainNodePath& rackPath, int modIndex,
                                       bool tempoSync) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].tempoSync = tempoSync;
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModSyncDivision(const ChainNodePath& rackPath, int modIndex,
                                          SyncDivision division) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].syncDivision = division;
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModTriggerMode(const ChainNodePath& rackPath, int modIndex,
                                         LFOTriggerMode mode) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].triggerMode = mode;
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModCurvePreset(const ChainNodePath& rackPath, int modIndex,
                                         CurvePreset preset) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].curvePreset = preset;
        notifyDeviceModifiersChanged(rackPath.trackId);
    }
}

void TrackManager::notifyRackModCurveChanged(const ChainNodePath& rackPath) {
    notifyDeviceModifiersChanged(rackPath.trackId);
}

void TrackManager::setRackModAudioAttack(const ChainNodePath& rackPath, int modIndex, float ms) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            rack->mods[modIndex].audioAttackMs = juce::jlimit(0.1f, 500.0f, ms);
        }
    }
}

void TrackManager::setRackModAudioRelease(const ChainNodePath& rackPath, int modIndex, float ms) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            rack->mods[modIndex].audioReleaseMs = juce::jlimit(1.0f, 2000.0f, ms);
        }
    }
}

void TrackManager::addRackMod(const ChainNodePath& rackPath, int slotIndex, ModType type,
                              LFOWaveform waveform) {
    if (auto* rack = getRackByPath(rackPath)) {
        // Add a single mod at the specified slot index
        if (slotIndex >= 0 && slotIndex <= static_cast<int>(rack->mods.size())) {
            ModInfo newMod(slotIndex);
            newMod.type = type;
            newMod.waveform = waveform;
            // Use "Curve" name for custom waveform
            if (waveform == LFOWaveform::Custom) {
                newMod.name = "Curve " + juce::String(slotIndex + 1);
            } else {
                newMod.name = ModInfo::getDefaultName(slotIndex, type);
            }
            rack->mods.insert(rack->mods.begin() + slotIndex, newMod);

            // Update IDs for mods after the inserted one
            for (int i = slotIndex + 1; i < static_cast<int>(rack->mods.size()); ++i) {
                rack->mods[i].id = i;
            }

            // Don't notify - caller handles UI update to avoid panel closing
        }
    }
}

void TrackManager::removeRackMod(const ChainNodePath& rackPath, int modIndex) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            rack->mods.erase(rack->mods.begin() + modIndex);

            // Update IDs for remaining mods
            for (int i = modIndex; i < static_cast<int>(rack->mods.size()); ++i) {
                rack->mods[i].id = i;
                rack->mods[i].name = ModInfo::getDefaultName(i, rack->mods[i].type);
            }

            // Notify asynchronously so the UI callback can unwind before rebuild
            auto trackId = rackPath.trackId;
            juce::MessageManager::callAsync([trackId]() {
                if (juce::JUCEApplicationBase::getInstance() == nullptr)
                    return;
                TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
            });
        }
    }
}

void TrackManager::setRackModEnabled(const ChainNodePath& rackPath, int modIndex, bool enabled) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            rack->mods[modIndex].enabled = enabled;
            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

void TrackManager::addRackModPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        addModPage(rack->mods);
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::removeRackModPage(const ChainNodePath& rackPath) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (removeModPage(rack->mods)) {
            notifyTrackDevicesChanged(rackPath.trackId);
        }
    }
}

// ============================================================================
// Device Mod Management
// ============================================================================

// Helper: get a ModInfo from device path + index, returns {mod, trackId} or {nullptr, invalid}
ModInfo* TrackManager::getDeviceMod(const ChainNodePath& devicePath, int modIndex) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(device->mods.size())) {
            return &device->mods[modIndex];
        }
    }
    return nullptr;
}

void TrackManager::setDeviceModAmount(const ChainNodePath& devicePath, int modIndex, float amount) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->amount = juce::jlimit(0.0f, 1.0f, amount);
    }
}

void TrackManager::setDeviceModTarget(const ChainNodePath& devicePath, int modIndex,
                                      ModTarget target) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        if (target.isValid()) {
            mod->addLink(target, 0.5f);
        }
        mod->target = target;
        // Use modifier-only notify to avoid full UI rebuild (panel stays open)
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::removeDeviceModLink(const ChainNodePath& devicePath, int modIndex,
                                       ModTarget target) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->removeLink(target);
        if (mod->target == target) {
            mod->target = ModTarget{};
        }
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModLinkAmount(const ChainNodePath& devicePath, int modIndex,
                                          ModTarget target, float amount) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        if (auto* link = mod->getLink(target)) {
            link->amount = amount;
        } else {
            mod->links.push_back({target, amount});
        }
        if (mod->target == target) {
            mod->amount = amount;
        }
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModName(const ChainNodePath& devicePath, int modIndex,
                                    const juce::String& name) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->name = name;
    }
}

void TrackManager::setDeviceModType(const ChainNodePath& devicePath, int modIndex, ModType type) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        auto oldType = mod->type;
        mod->type = type;
        auto defaultOldName = ModInfo::getDefaultName(modIndex, oldType);
        if (mod->name == defaultOldName) {
            mod->name = ModInfo::getDefaultName(modIndex, type);
        }
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModWaveform(const ChainNodePath& devicePath, int modIndex,
                                        LFOWaveform waveform) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->waveform = waveform;
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModRate(const ChainNodePath& devicePath, int modIndex, float rate) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->rate = rate;
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModPhaseOffset(const ChainNodePath& devicePath, int modIndex,
                                           float phaseOffset) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModTempoSync(const ChainNodePath& devicePath, int modIndex,
                                         bool tempoSync) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->tempoSync = tempoSync;
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModSyncDivision(const ChainNodePath& devicePath, int modIndex,
                                            SyncDivision division) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->syncDivision = division;
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModTriggerMode(const ChainNodePath& devicePath, int modIndex,
                                           LFOTriggerMode mode) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->triggerMode = mode;
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModCurvePreset(const ChainNodePath& devicePath, int modIndex,
                                           CurvePreset preset) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->curvePreset = preset;
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::notifyDeviceModCurveChanged(const ChainNodePath& devicePath) {
    notifyDeviceModifiersChanged(devicePath.trackId);
}

void TrackManager::setDeviceModAudioAttack(const ChainNodePath& devicePath, int modIndex,
                                           float ms) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->audioAttackMs = juce::jlimit(0.1f, 500.0f, ms);
    }
}

void TrackManager::setDeviceModAudioRelease(const ChainNodePath& devicePath, int modIndex,
                                            float ms) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->audioReleaseMs = juce::jlimit(1.0f, 2000.0f, ms);
    }
}

void TrackManager::addDeviceMod(const ChainNodePath& devicePath, int slotIndex, ModType type,
                                LFOWaveform waveform) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        // Add a single mod at the specified slot index
        if (slotIndex >= 0 && slotIndex <= static_cast<int>(device->mods.size())) {
            ModInfo newMod(slotIndex);
            newMod.type = type;
            newMod.waveform = waveform;
            // Use "Curve" name for custom waveform
            if (waveform == LFOWaveform::Custom) {
                newMod.name = "Curve " + juce::String(slotIndex + 1);
            } else {
                newMod.name = ModInfo::getDefaultName(slotIndex, type);
            }
            device->mods.insert(device->mods.begin() + slotIndex, newMod);

            // Update IDs for mods after the inserted one
            for (int i = slotIndex + 1; i < static_cast<int>(device->mods.size()); ++i) {
                device->mods[i].id = i;
            }

            // Don't notify - caller handles UI update to avoid panel closing
        }
    }
}

void TrackManager::removeDeviceMod(const ChainNodePath& devicePath, int modIndex) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(device->mods.size())) {
            device->mods.erase(device->mods.begin() + modIndex);

            // Update IDs for remaining mods
            for (int i = modIndex; i < static_cast<int>(device->mods.size()); ++i) {
                device->mods[i].id = i;
                device->mods[i].name = ModInfo::getDefaultName(i, device->mods[i].type);
            }

            // Notify asynchronously so the UI callback can unwind before rebuild
            auto trackId = devicePath.trackId;
            juce::MessageManager::callAsync([trackId]() {
                if (juce::JUCEApplicationBase::getInstance() == nullptr)
                    return;
                TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
            });
        }
    }
}

void TrackManager::setDeviceModEnabled(const ChainNodePath& devicePath, int modIndex,
                                       bool enabled) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(device->mods.size())) {
            device->mods[modIndex].enabled = enabled;
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

void TrackManager::addDeviceModPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        addModPage(device->mods);
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::removeDeviceModPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (removeModPage(device->mods)) {
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

void TrackManager::triggerMidiNoteOn(TrackId trackId) {
    std::lock_guard<std::mutex> lock(midiTriggerMutex_);
    pendingMidiTriggers_.insert(trackId);
}

const ModInfo* TrackManager::getModById(TrackId trackId, ModId modId) const {
    const auto* track = getTrack(trackId);
    if (!track)
        return nullptr;
    for (const auto& element : track->chainElements) {
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
    pendingMidiNoteOffs_.insert(trackId);
}

TrackManager::TransportSnapshot TrackManager::consumeTransportState() {
    return {transportBpm_.load(std::memory_order_acquire),
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
                                 bool transportJustLooped, bool transportJustStopped) {
    // Snapshot MIDI triggers (thread-safe)
    std::set<TrackId> midiTriggeredTracks;
    std::set<TrackId> midiNoteOffTracks;
    {
        std::lock_guard<std::mutex> lock(midiTriggerMutex_);
        midiTriggeredTracks.swap(pendingMidiTriggers_);
        midiNoteOffTracks.swap(pendingMidiNoteOffs_);
    }

    // Read audio-thread sidechain triggers from the lock-free bus
    auto& bus = SidechainTriggerBus::getInstance();
    std::array<float, kMaxBusTracks> audioPeakLevels{};
    for (const auto& track : tracks_) {
        if (track.id < 0 || track.id >= kMaxBusTracks)
            continue;
        uint64_t currentNoteOn = bus.getNoteOnCounter(track.id);
        uint64_t currentNoteOff = bus.getNoteOffCounter(track.id);
        if (currentNoteOn != lastBusNoteOn_[track.id]) {
            midiTriggeredTracks.insert(track.id);
            lastBusNoteOn_[track.id] = currentNoteOn;
        }
        if (currentNoteOff != lastBusNoteOff_[track.id]) {
            midiNoteOffTracks.insert(track.id);
            lastBusNoteOff_[track.id] = currentNoteOff;
        }
        audioPeakLevels[track.id] = bus.getAudioPeakLevel(track.id);
    }

    // Lambda to update a single mod's phase and value.
    // Returns true if 'running' state changed (needs TE assignment sync).
    auto updateMod = [deltaTime, bpm, transportJustStarted, transportJustLooped,
                      transportJustStopped](ModInfo& mod, bool midiTriggered, bool midiNoteOff,
                                            float audioPeakLevel) -> bool {
        bool wasRunning = mod.running;
        // Skip disabled mods - set value to 0 so they don't affect modulation
        if (!mod.enabled) {
            mod.value = 0.0f;
            mod.triggered = false;
            return false;
        }

        if (mod.type == ModType::LFO) {
            // Check for trigger (phase reset)
            bool shouldTrigger = false;
            switch (mod.triggerMode) {
                case LFOTriggerMode::Free:
                    break;
                case LFOTriggerMode::Transport:
                    if (transportJustStarted || transportJustLooped)
                        shouldTrigger = true;
                    break;
                case LFOTriggerMode::MIDI:
                    if (midiTriggered)
                        shouldTrigger = true;
                    break;
                case LFOTriggerMode::Audio: {
                    // Envelope follower: smooth peak level with attack/release
                    float attackCoeff = 1.0f;
                    float releaseCoeff = 1.0f;
                    if (mod.audioAttackMs > 0.0f)
                        attackCoeff = 1.0f - std::exp(-static_cast<float>(deltaTime) /
                                                      (mod.audioAttackMs * 0.001f));
                    if (mod.audioReleaseMs > 0.0f)
                        releaseCoeff = 1.0f - std::exp(-static_cast<float>(deltaTime) /
                                                       (mod.audioReleaseMs * 0.001f));

                    if (audioPeakLevel > mod.audioEnvLevel)
                        mod.audioEnvLevel += attackCoeff * (audioPeakLevel - mod.audioEnvLevel);
                    else
                        mod.audioEnvLevel += releaseCoeff * (audioPeakLevel - mod.audioEnvLevel);

                    // Transient detection: trigger when raw peak crosses above threshold,
                    // re-arm when it drops back below the same threshold.
                    constexpr float threshold = 0.1f;  // ~-20dB
                    if (!mod.audioGateOpen && audioPeakLevel > threshold) {
                        mod.audioGateOpen = true;
                        shouldTrigger = true;
                    } else if (mod.audioGateOpen && audioPeakLevel < threshold) {
                        mod.audioGateOpen = false;
                    }
                    break;
                }
            }

            // Handle note-off: stop MIDI-triggered LFOs
            if (mod.triggerMode == LFOTriggerMode::MIDI && midiNoteOff && mod.running)
                mod.running = false;

            // Handle audio gate close: stop Audio-triggered LFOs
            if (mod.triggerMode == LFOTriggerMode::Audio && !mod.audioGateOpen && mod.running)
                mod.running = false;

            // Handle transport stop: stop Transport-triggered LFOs and reset phase
            if (mod.triggerMode == LFOTriggerMode::Transport && transportJustStopped &&
                mod.running) {
                mod.running = false;
                mod.phase = 0.0f;
            }

            if (shouldTrigger) {
                mod.phase = 0.0f;
                mod.triggered = true;
                mod.triggerCount++;
                mod.running = true;
            } else {
                mod.triggered = false;
            }

            // Gate: only advance phase for Free mode, or when running for triggered modes
            bool shouldAdvance = (mod.triggerMode == LFOTriggerMode::Free) || mod.running;

            if (shouldAdvance) {
                // Calculate effective rate (Hz or tempo-synced)
                float effectiveRate = mod.rate;
                if (mod.tempoSync) {
                    effectiveRate = ModulatorEngine::calculateSyncRateHz(mod.syncDivision, bpm);
                }

                // Update phase
                mod.phase += static_cast<float>(effectiveRate * deltaTime);
                if (mod.oneShot) {
                    // One-shot: clamp at end of cycle, hold final value
                    if (mod.phase >= 1.0f) {
                        mod.phase = 1.0f;
                        mod.running = false;
                    }
                } else {
                    // Loop: wrap at 1.0
                    while (mod.phase >= 1.0f)
                        mod.phase -= 1.0f;
                }
                // Apply phase offset when generating waveform
                // Use 0.999999f for one-shot end so curve evaluation stays in the
                // last segment rather than wrapping to the first point.
                float effectivePhase = mod.oneShot && mod.phase >= 1.0f
                                           ? 0.999999f
                                           : std::fmod(mod.phase + mod.phaseOffset, 1.0f);
                mod.value = ModulatorEngine::generateWaveformForMod(mod, effectivePhase);
            } else {
                mod.value = 0.0f;  // No output when not running
            }
        }
        return mod.running != wasRunning;
    };

    // Recursive lambda to update mods in chain elements
    // Returns true if any mod's running state changed
    std::function<bool(ChainElement&, bool, bool, float)> updateElementMods =
        [&](ChainElement& element, bool midiTriggered, bool midiNoteOff, float audioPeak) -> bool {
        bool changed = false;
        if (isDevice(element)) {
            DeviceInfo& device = magda::getDevice(element);
            bool deviceMidiTriggered = midiTriggered;
            bool deviceMidiNoteOff = midiNoteOff;
            float deviceAudioPeak = audioPeak;

            // Cross-track sidechain: use source track's MIDI and audio
            if (device.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                auto srcId = device.sidechain.sourceTrackId;
                // MIDI triggers from source track
                if (midiTriggeredTracks.count(srcId) > 0)
                    deviceMidiTriggered = true;
                if (midiNoteOffTracks.count(srcId) > 0)
                    deviceMidiNoteOff = true;

                // Audio peak from source track (for Audio-triggered mods)
                if (srcId >= 0 && srcId < kMaxBusTracks)
                    deviceAudioPeak = audioPeakLevels[srcId];
            }

            for (auto& mod : device.mods) {
                changed |= updateMod(mod, deviceMidiTriggered, deviceMidiNoteOff, deviceAudioPeak);
            }
        } else if (isRack(element)) {
            RackInfo& rack = magda::getRack(element);
            bool rackMidiTriggered = midiTriggered;
            bool rackMidiNoteOff = midiNoteOff;
            float rackAudioPeak = audioPeak;

            // Check rack-level sidechain source
            if (rack.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                auto srcId = rack.sidechain.sourceTrackId;
                if (midiTriggeredTracks.count(srcId) > 0)
                    rackMidiTriggered = true;
                if (midiNoteOffTracks.count(srcId) > 0)
                    rackMidiNoteOff = true;
                if (srcId >= 0 && srcId < kMaxBusTracks)
                    rackAudioPeak = audioPeakLevels[srcId];
            }

            // Check devices inside the rack for sidechain sources
            for (const auto& chain : rack.chains) {
                for (const auto& chainElement : chain.elements) {
                    if (isDevice(chainElement)) {
                        const auto& dev = magda::getDevice(chainElement);
                        if (dev.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                            auto srcId = dev.sidechain.sourceTrackId;
                            if (midiTriggeredTracks.count(srcId) > 0)
                                rackMidiTriggered = true;
                            if (midiNoteOffTracks.count(srcId) > 0)
                                rackMidiNoteOff = true;
                            if (srcId >= 0 && srcId < kMaxBusTracks)
                                rackAudioPeak = audioPeakLevels[srcId];
                            break;
                        }
                    }
                }
            }

            for (auto& mod : rack.mods) {
                bool wasRunningBefore = mod.running;
                changed |= updateMod(mod, rackMidiTriggered, rackMidiNoteOff, rackAudioPeak);
                if (mod.running && !wasRunningBefore)
                    DBG("updateAllMods: rack mod "
                        << mod.id << " triggered! rackId=" << rack.id
                        << " rackMidiTriggered=" << (int)rackMidiTriggered);
            }
            for (auto& chain : rack.chains) {
                for (auto& chainElement : chain.elements) {
                    changed |= updateElementMods(chainElement, rackMidiTriggered, rackMidiNoteOff,
                                                 rackAudioPeak);
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
        bool trackMidiTriggered = midiTriggeredTracks.count(track.id) > 0;
        bool trackMidiNoteOff = midiNoteOffTracks.count(track.id) > 0;
        float trackAudioPeak = audioPeakLevels[track.id];
        bool trackChanged = false;
        for (auto& element : track.chainElements) {
            trackChanged |=
                updateElementMods(element, trackMidiTriggered, trackMidiNoteOff, trackAudioPeak);
        }
        if (trackChanged)
            tracksNeedingSync.push_back(track.id);
    }

    // Notify TE to sync assignment values for tracks where running state changed
    for (auto trackId : tracksNeedingSync) {
        notifyDeviceModifiersChanged(trackId);
    }
}

// ============================================================================
// Device Macro Management
// ============================================================================

void TrackManager::setDeviceMacroValue(const ChainNodePath& devicePath, int macroIndex,
                                       float value) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        float clampedValue = juce::jlimit(0.0f, 1.0f, value);
        device->macros[macroIndex].value = clampedValue;
        notifyMacroValueChanged(devicePath.trackId, false, device->id, macroIndex, clampedValue);
    }
}

void TrackManager::setDeviceMacroTarget(const ChainNodePath& devicePath, int macroIndex,
                                        MacroTarget target) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }

        // Add to links vector if not already present
        if (!device->macros[macroIndex].getLink(target)) {
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = 0.5f;  // Default amount
            device->macros[macroIndex].links.push_back(newLink);
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

void TrackManager::removeDeviceMacroLink(const ChainNodePath& devicePath, int macroIndex,
                                         MacroTarget target) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        device->macros[macroIndex].removeLink(target);
    }
}

void TrackManager::setDeviceMacroLinkAmount(const ChainNodePath& devicePath, int macroIndex,
                                            MacroTarget target, float amount) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        bool created = false;
        if (auto* link = device->macros[macroIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            device->macros[macroIndex].links.push_back(newLink);
            created = true;
        }
        if (created) {
            notifyTrackDevicesChanged(devicePath.trackId);
        } else {
            // Existing link amount changed — resync TE assignments
            notifyDeviceModifiersChanged(devicePath.trackId);
        }
    }
}

void TrackManager::setDeviceMacroName(const ChainNodePath& devicePath, int macroIndex,
                                      const juce::String& name) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        device->macros[macroIndex].name = name;
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::addDeviceMacroPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        addMacroPage(device->macros);
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::removeDeviceMacroPage(const ChainNodePath& devicePath) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (removeMacroPage(device->macros)) {
            notifyTrackDevicesChanged(devicePath.trackId);
        }
    }
}

}  // namespace magda
