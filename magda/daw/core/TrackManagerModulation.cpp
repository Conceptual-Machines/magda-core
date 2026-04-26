#include <cmath>
#include <set>

#include "../audio/SidechainTriggerBus.hpp"
#include "ModulatorEngine.hpp"
#include "RackInfo.hpp"
#include "TrackManager.hpp"

namespace magda {

namespace {

struct ModTickInputs {
    bool midiTriggered = false;
    bool midiNoteOff = false;
    float audioPeakLevel = 0.0f;
    double deltaTime = 0.0;
    double bpm = 120.0;
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
        // ModParam targets are only meaningful through the links vector — the
        // legacy single-target field has no rendering path for cross-mod links.
        if (target.kind == MacroTarget::Kind::ModParam) {
            if (!rack->macros[macroIndex].getLink(target)) {
                MacroLink newLink;
                newLink.target = target;
                newLink.amount = 1.0f;
                rack->macros[macroIndex].links.push_back(newLink);
                notifyDeviceModifiersChanged(rackPath.trackId);
            }
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
        // Notify when a new link is created (needs TE modifier assignment).
        // ModParam targets don't change device topology — keep the lighter
        // notification so an open mod editor doesn't get torn down.
        if (created && target.kind != MacroTarget::Kind::ModParam) {
            notifyTrackDevicesChanged(rackPath.trackId);
        } else {
            // Existing link amount changed — resync TE assignments
            notifyDeviceModifiersChanged(rackPath.trackId);
        }
    }
}

void TrackManager::setRackMacroLinkBipolar(const ChainNodePath& rackPath, int macroIndex,
                                           MacroTarget target, bool bipolar) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        if (auto* link = rack->macros[macroIndex].getLink(target)) {
            link->bipolar = bipolar;
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

void TrackManager::removeRackMacroLink(const ChainNodePath& rackPath, int macroIndex,
                                       MacroTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(rack->macros.size())) {
            return;
        }
        rack->macros[macroIndex].removeLink(target);
        notifyTrackDevicesChanged(rackPath.trackId);
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
        rack->mods[modIndex].amount = juce::jlimit(-1.0f, 1.0f, amount);
        // Don't notify - simple value change doesn't need UI rebuild
    }
}

void TrackManager::setRackModTarget(const ChainNodePath& rackPath, int modIndex, ModTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        // ModParam targets are only meaningful through the links vector — the
        // legacy single-target field has no rendering path for cross-mod links.
        if (target.kind == ModTarget::Kind::ModParam) {
            if (!rack->mods[modIndex].getLink(target)) {
                ModLink newLink;
                newLink.target = target;
                newLink.amount = 1.0f;
                rack->mods[modIndex].links.push_back(newLink);
                notifyDeviceModifiersChanged(rackPath.trackId);
            }
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

void TrackManager::setRackModLinkBipolar(const ChainNodePath& rackPath, int modIndex,
                                         ModTarget target, bool bipolar) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        if (auto* link = rack->mods[modIndex].getLink(target)) {
            link->bipolar = bipolar;
            notifyDeviceModifiersChanged(rackPath.trackId);
        }
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
        notifyModParameterChanged(rackPath.trackId, rackPath, rack->mods[modIndex].id,
                                  /*paramIndex=*/0, rate);
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
        notifyModParameterChanged(rackPath.trackId, rackPath, rack->mods[modIndex].id,
                                  /*paramIndex=*/1,
                                  static_cast<float>(syncDivisionToTeRateOrdinal(division)));
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

void TrackManager::removeRackModLink(const ChainNodePath& rackPath, int modIndex,
                                     ModTarget target) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex >= 0 && modIndex < static_cast<int>(rack->mods.size())) {
            auto& mod = rack->mods[modIndex];
            mod.removeLink(target);
            if (mod.target == target) {
                mod.target = ModTarget{};
            }
            notifyTrackDevicesChanged(rackPath.trackId);
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
        mod->amount = juce::jlimit(-1.0f, 1.0f, amount);
    }
}

void TrackManager::setDeviceModTarget(const ChainNodePath& devicePath, int modIndex,
                                      ModTarget target) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        if (target.isValid()) {
            // ModParam picks come from a menu (no drag), so 0.0 would be silent —
            // give the link an audible default amount the user can dial down.
            const float defaultAmount = target.kind == ModTarget::Kind::ModParam ? 1.0f : 0.0f;
            mod->addLink(target, defaultAmount);
        }
        if (target.kind != ModTarget::Kind::ModParam)
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

void TrackManager::setDeviceModLinkBipolar(const ChainNodePath& devicePath, int modIndex,
                                           ModTarget target, bool bipolar) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        if (auto* link = mod->getLink(target)) {
            link->bipolar = bipolar;
            notifyDeviceModifiersChanged(devicePath.trackId);
        }
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
        notifyModParameterChanged(devicePath.trackId, devicePath, mod->id, /*paramIndex=*/0, rate);
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
        notifyModParameterChanged(devicePath.trackId, devicePath, mod->id, /*paramIndex=*/1,
                                  static_cast<float>(syncDivisionToTeRateOrdinal(division)));
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
    pendingMidiNoteOffs_[trackId]++;
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
    auto updateMod = [deltaTime, bpm, transportJustStarted, transportJustLooped,
                      transportJustStopped](ModInfo& mod, bool midiTriggered, bool midiNoteOff,
                                            float audioPeakLevel,
                                            const std::vector<MacroInfo>& scopeMacros,
                                            const std::vector<ModInfo>& scopeMods) -> bool {
        bool wasRunning = mod.running;
        ModTickInputs inputs{
            midiTriggered, midiNoteOff,          audioPeakLevel,      deltaTime,
            bpm,           transportJustStarted, transportJustLooped, transportJustStopped};
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
                        if (l.target.kind != MacroTarget::Kind::ModParam ||
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
                        if (l.target.kind != ModTarget::Kind::ModParam ||
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

            // Check rack-level sidechain source — replaces self triggers
            if (rack.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                auto srcId = rack.sidechain.sourceTrackId;
                rackMidiTriggered = midiNoteOnTracks.count(srcId) > 0;
                rackMidiNoteOff = midiAllNotesOffTracks.count(srcId) > 0;
                if (srcId >= 0 && srcId < kMaxBusTracks)
                    rackAudioPeak = audioPeakLevels[srcId];
            }

            // Check devices inside the rack for sidechain sources — replaces self triggers
            for (const auto& chain : rack.chains) {
                for (const auto& chainElement : chain.elements) {
                    if (isDevice(chainElement)) {
                        const auto& dev = magda::getDevice(chainElement);
                        if (dev.sidechain.sourceTrackId != INVALID_TRACK_ID) {
                            auto srcId = dev.sidechain.sourceTrackId;
                            rackMidiTriggered = midiNoteOnTracks.count(srcId) > 0;
                            rackMidiNoteOff = midiAllNotesOffTracks.count(srcId) > 0;
                            if (srcId >= 0 && srcId < kMaxBusTracks)
                                rackAudioPeak = audioPeakLevels[srcId];
                            break;
                        }
                    }
                }
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
        for (auto& element : track.chainElements) {
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

// ============================================================================
// Device Macro Management
// ============================================================================

void TrackManager::setDeviceMacroValue(const ChainNodePath& devicePath, int macroIndex,
                                       float value) {
    auto* device = getDeviceInChainByPath(devicePath);
    if (!device)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size()))
        return;
    float clampedValue = juce::jlimit(0.0f, 1.0f, value);
    device->macros[macroIndex].value = clampedValue;
    notifyMacroValueChanged(devicePath.trackId, false, device->id, macroIndex, clampedValue);
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
            // ModParam picks come from a menu (no drag overlay yet); 100% so
            // the link is immediately audible. Knob-target picks default to
            // 30% to leave headroom for the overlay drag.
            newLink.amount = target.kind == MacroTarget::Kind::ModParam ? 1.0f : 0.3f;
            newLink.bipolar = false;  // Default unipolar
            device->macros[macroIndex].links.push_back(newLink);
            // Use lighter notification — adding a macro link doesn't change device
            // structure, and a full rebuild would destroy the active link mode UI.
            notifyDeviceModifiersChanged(devicePath.trackId);
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
        notifyDeviceModifiersChanged(devicePath.trackId);
    }
}

void TrackManager::clearAllDeviceMacroLinks(const ChainNodePath& devicePath, int macroIndex) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        device->macros[macroIndex].links.clear();
        device->macros[macroIndex].target = MacroTarget{};
        notifyDeviceModifiersChanged(devicePath.trackId);
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
        // ModParam links don't change device topology — keep the lighter
        // notification so an open mod editor isn't torn down.
        if (created && target.kind != MacroTarget::Kind::ModParam) {
            notifyTrackDevicesChanged(devicePath.trackId);
        } else {
            // Existing link amount changed — resync TE assignments
            notifyDeviceModifiersChanged(devicePath.trackId);
        }
    }
}

void TrackManager::setDeviceMacroLinkBipolar(const ChainNodePath& devicePath, int macroIndex,
                                             MacroTarget target, bool bipolar) {
    if (auto* device = getDeviceInChainByPath(devicePath)) {
        if (macroIndex < 0 || macroIndex >= static_cast<int>(device->macros.size())) {
            return;
        }
        if (auto* link = device->macros[macroIndex].getLink(target)) {
            link->bipolar = bipolar;
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

// ============================================================================
// Track-Level Mod Management
// ============================================================================

ModInfo* TrackManager::getTrackMod(TrackId trackId, int modIndex) {
    auto* track = getTrack(trackId);
    if (!track)
        return nullptr;
    if (modIndex >= 0 && modIndex < static_cast<int>(track->mods.size()))
        return &track->mods[modIndex];
    return nullptr;
}

void TrackManager::setTrackModAmount(TrackId trackId, int modIndex, float amount) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->amount = juce::jlimit(-1.0f, 1.0f, amount);
    }
}

void TrackManager::setTrackModTarget(TrackId trackId, int modIndex, ModTarget target) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        if (target.isValid()) {
            // ModParam picks come from a menu (no drag), so 0.0 would be silent —
            // give the link an audible default amount the user can dial down.
            const float defaultAmount = target.kind == ModTarget::Kind::ModParam ? 1.0f : 0.0f;
            mod->addLink(target, defaultAmount);
        }
        if (target.kind != ModTarget::Kind::ModParam)
            mod->target = target;
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackModLinkAmount(TrackId trackId, int modIndex, ModTarget target,
                                         float amount) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        if (auto* link = mod->getLink(target)) {
            link->amount = amount;
        } else {
            mod->links.push_back({target, amount});
        }
        if (mod->target == target) {
            mod->amount = amount;
        }
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackModLinkBipolar(TrackId trackId, int modIndex, ModTarget target,
                                          bool bipolar) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        if (auto* link = mod->getLink(target)) {
            link->bipolar = bipolar;
            notifyDeviceModifiersChanged(trackId);
        }
    }
}

void TrackManager::setTrackModName(TrackId trackId, int modIndex, const juce::String& name) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->name = name;
    }
}

void TrackManager::setTrackModType(TrackId trackId, int modIndex, ModType type) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        auto oldType = mod->type;
        mod->type = type;
        auto defaultOldName = ModInfo::getDefaultName(modIndex, oldType);
        if (mod->name == defaultOldName) {
            mod->name = ModInfo::getDefaultName(modIndex, type);
        }
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setTrackModWaveform(TrackId trackId, int modIndex, LFOWaveform waveform) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->waveform = waveform;
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackModRate(TrackId trackId, int modIndex, float rate) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->rate = rate;
        notifyDeviceModifiersChanged(trackId);
        notifyModParameterChanged(trackId, ChainNodePath::trackLevel(trackId), mod->id,
                                  /*paramIndex=*/0, rate);
    }
}

void TrackManager::setTrackModPhaseOffset(TrackId trackId, int modIndex, float phaseOffset) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackModTempoSync(TrackId trackId, int modIndex, bool tempoSync) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->tempoSync = tempoSync;
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackModSyncDivision(TrackId trackId, int modIndex, SyncDivision division) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->syncDivision = division;
        notifyDeviceModifiersChanged(trackId);
        notifyModParameterChanged(trackId, ChainNodePath::trackLevel(trackId), mod->id,
                                  /*paramIndex=*/1,
                                  static_cast<float>(syncDivisionToTeRateOrdinal(division)));
    }
}

void TrackManager::setTrackModTriggerMode(TrackId trackId, int modIndex, LFOTriggerMode mode) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->triggerMode = mode;
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackModCurvePreset(TrackId trackId, int modIndex, CurvePreset preset) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->curvePreset = preset;
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::notifyTrackModCurveChanged(TrackId trackId) {
    notifyDeviceModifiersChanged(trackId);
}

void TrackManager::setTrackModAudioAttack(TrackId trackId, int modIndex, float ms) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->audioAttackMs = juce::jlimit(0.1f, 500.0f, ms);
    }
}

void TrackManager::setTrackModAudioRelease(TrackId trackId, int modIndex, float ms) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->audioReleaseMs = juce::jlimit(1.0f, 2000.0f, ms);
    }
}

void TrackManager::addTrackMod(TrackId trackId, int slotIndex, ModType type, LFOWaveform waveform) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (slotIndex >= 0 && slotIndex <= static_cast<int>(track->mods.size())) {
        ModInfo newMod(slotIndex);
        newMod.type = type;
        newMod.waveform = waveform;
        if (waveform == LFOWaveform::Custom) {
            newMod.name = "Curve " + juce::String(slotIndex + 1);
        } else {
            newMod.name = ModInfo::getDefaultName(slotIndex, type);
        }
        track->mods.insert(track->mods.begin() + slotIndex, newMod);

        // Update IDs for mods after the inserted one
        for (int i = slotIndex + 1; i < static_cast<int>(track->mods.size()); ++i) {
            track->mods[i].id = i;
        }

        // Don't notify - caller handles UI update to avoid panel closing
    }
}

void TrackManager::removeTrackMod(TrackId trackId, int modIndex) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (modIndex >= 0 && modIndex < static_cast<int>(track->mods.size())) {
        track->mods.erase(track->mods.begin() + modIndex);

        // Update IDs for remaining mods
        for (int i = modIndex; i < static_cast<int>(track->mods.size()); ++i) {
            track->mods[i].id = i;
            track->mods[i].name = ModInfo::getDefaultName(i, track->mods[i].type);
        }

        // Notify asynchronously so the UI callback can unwind before rebuild
        juce::MessageManager::callAsync([trackId]() {
            if (juce::JUCEApplicationBase::getInstance() == nullptr)
                return;
            TrackManager::getInstance().notifyTrackDevicesChanged(trackId);
        });
    }
}

void TrackManager::removeTrackModLink(TrackId trackId, int modIndex, ModTarget target) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->removeLink(target);
        if (mod->target == target) {
            mod->target = ModTarget{};
        }
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::setTrackModEnabled(TrackId trackId, int modIndex, bool enabled) {
    if (auto* mod = getTrackMod(trackId, modIndex)) {
        mod->enabled = enabled;
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::addTrackModPage(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (track) {
        addModPage(track->mods);
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::removeTrackModPage(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (track) {
        if (removeModPage(track->mods)) {
            notifyTrackDevicesChanged(trackId);
        }
    }
}

// ============================================================================
// Track-Level Macro Management
// ============================================================================

void TrackManager::setTrackMacroValue(TrackId trackId, int macroIndex, float value) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    float clampedValue = juce::jlimit(0.0f, 1.0f, value);
    track->macros[macroIndex].value = clampedValue;
    notifyMacroValueChanged(trackId, false, trackId, macroIndex, clampedValue);
}

void TrackManager::setTrackMacroTarget(TrackId trackId, int macroIndex, MacroTarget target) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    if (!track->macros[macroIndex].getLink(target)) {
        MacroLink newLink;
        newLink.target = target;
        // ModParam picks come from a menu (no drag overlay yet); 100% so
        // the link is immediately audible. Knob-target picks default to
        // 30% to leave headroom for the overlay drag.
        newLink.amount = target.kind == MacroTarget::Kind::ModParam ? 1.0f : 0.3f;
        track->macros[macroIndex].links.push_back(newLink);
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackMacroLinkAmount(TrackId trackId, int macroIndex, MacroTarget target,
                                           float amount) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    bool created = false;
    if (auto* link = track->macros[macroIndex].getLink(target)) {
        link->amount = amount;
    } else {
        MacroLink newLink;
        newLink.target = target;
        newLink.amount = amount;
        track->macros[macroIndex].links.push_back(newLink);
        created = true;
    }
    if (created && target.kind != MacroTarget::Kind::ModParam) {
        // ModParam links don't change device topology — only the modifier
        // attachment graph. The lighter resync covers that and avoids
        // collapsing any open mod editor (which lives inside a NodeComponent
        // that trackDevicesChanged would rebuild).
        notifyTrackDevicesChanged(trackId);
    } else {
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackMacroLinkBipolar(TrackId trackId, int macroIndex, MacroTarget target,
                                            bool bipolar) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    if (auto* link = track->macros[macroIndex].getLink(target)) {
        link->bipolar = bipolar;
        notifyDeviceModifiersChanged(trackId);
    }
}

void TrackManager::setTrackMacroName(TrackId trackId, int macroIndex, const juce::String& name) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    track->macros[macroIndex].name = name;
}

void TrackManager::removeTrackMacroLink(TrackId trackId, int macroIndex, MacroTarget target) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    track->macros[macroIndex].removeLink(target);
    notifyTrackDevicesChanged(trackId);
}

void TrackManager::clearAllTrackMacroLinks(TrackId trackId, int macroIndex) {
    auto* track = getTrack(trackId);
    if (!track)
        return;
    if (macroIndex < 0 || macroIndex >= static_cast<int>(track->macros.size()))
        return;
    track->macros[macroIndex].links.clear();
    track->macros[macroIndex].target = MacroTarget{};
    notifyTrackDevicesChanged(trackId);
}

void TrackManager::addTrackMacroPage(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (track) {
        addMacroPage(track->macros);
        notifyTrackDevicesChanged(trackId);
    }
}

void TrackManager::removeTrackMacroPage(TrackId trackId) {
    auto* track = getTrack(trackId);
    if (track) {
        if (removeMacroPage(track->macros)) {
            notifyTrackDevicesChanged(trackId);
        }
    }
}

}  // namespace magda
