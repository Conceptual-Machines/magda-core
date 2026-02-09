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
        rack->macros[macroIndex].value = juce::jlimit(0.0f, 1.0f, value);
        // Don't notify - simple value change doesn't need UI rebuild
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
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModLinkAmount(const ChainNodePath& rackPath, int modIndex,
                                        ModTarget target, float amount) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        // Update amount in links vector (or create link if it doesn't exist)
        bool created = false;
        if (auto* link = rack->mods[modIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            ModLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            rack->mods[modIndex].links.push_back(newLink);
            created = true;
        }
        // Also update legacy amount if target matches
        if (rack->mods[modIndex].target == target) {
            rack->mods[modIndex].amount = amount;
        }
        // Notify when a new link is created (needs TE modifier assignment)
        if (created) {
            notifyTrackDevicesChanged(rackPath.trackId);
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
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModRate(const ChainNodePath& rackPath, int modIndex, float rate) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].rate = rate;
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModPhaseOffset(const ChainNodePath& rackPath, int modIndex,
                                         float phaseOffset) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModTempoSync(const ChainNodePath& rackPath, int modIndex,
                                       bool tempoSync) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].tempoSync = tempoSync;
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModSyncDivision(const ChainNodePath& rackPath, int modIndex,
                                          SyncDivision division) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].syncDivision = division;
        notifyTrackDevicesChanged(rackPath.trackId);
    }
}

void TrackManager::setRackModTriggerMode(const ChainNodePath& rackPath, int modIndex,
                                         LFOTriggerMode mode) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].triggerMode = mode;
    }
}

void TrackManager::setRackModCurvePreset(const ChainNodePath& rackPath, int modIndex,
                                         CurvePreset preset) {
    if (auto* rack = getRackByPath(rackPath)) {
        if (modIndex < 0 || modIndex >= static_cast<int>(rack->mods.size())) {
            return;
        }
        rack->mods[modIndex].curvePreset = preset;
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
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::removeDeviceModLink(const ChainNodePath& devicePath, int modIndex,
                                       ModTarget target) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->removeLink(target);
        if (mod->target == target) {
            mod->target = ModTarget{};
        }
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModLinkAmount(const ChainNodePath& devicePath, int modIndex,
                                          ModTarget target, float amount) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        if (auto* link = mod->getLink(target)) {
            link->amount = amount;
        } else {
            mod->links.push_back({target, amount});
            notifyTrackDevicesChanged(devicePath.trackId);
        }
        if (mod->target == target) {
            mod->amount = amount;
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
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModRate(const ChainNodePath& devicePath, int modIndex, float rate) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->rate = rate;
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModPhaseOffset(const ChainNodePath& devicePath, int modIndex,
                                           float phaseOffset) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->phaseOffset = juce::jlimit(0.0f, 1.0f, phaseOffset);
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModTempoSync(const ChainNodePath& devicePath, int modIndex,
                                         bool tempoSync) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->tempoSync = tempoSync;
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModSyncDivision(const ChainNodePath& devicePath, int modIndex,
                                            SyncDivision division) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->syncDivision = division;
        notifyTrackDevicesChanged(devicePath.trackId);
    }
}

void TrackManager::setDeviceModTriggerMode(const ChainNodePath& devicePath, int modIndex,
                                           LFOTriggerMode mode) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->triggerMode = mode;
    }
}

void TrackManager::setDeviceModCurvePreset(const ChainNodePath& devicePath, int modIndex,
                                           CurvePreset preset) {
    if (auto* mod = getDeviceMod(devicePath, modIndex)) {
        mod->curvePreset = preset;
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

            notifyTrackDevicesChanged(devicePath.trackId);
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

// ============================================================================
// Mod Updates
// ============================================================================

void TrackManager::updateAllMods(double deltaTime, double bpm, bool transportJustStarted,
                                 bool transportJustLooped) {
    // Lambda to update a single mod's phase and value
    auto updateMod = [deltaTime, bpm, transportJustStarted, transportJustLooped](ModInfo& mod) {
        // Skip disabled mods - set value to 0 so they don't affect modulation
        if (!mod.enabled) {
            mod.value = 0.0f;
            mod.triggered = false;
            return;
        }

        if (mod.type == ModType::LFO) {
            // Check for trigger (phase reset)
            bool shouldTrigger = false;
            switch (mod.triggerMode) {
                case LFOTriggerMode::Free:
                    // Never reset
                    break;
                case LFOTriggerMode::Transport:
                    // Reset on transport start or loop
                    if (transportJustStarted || transportJustLooped) {
                        shouldTrigger = true;
                    }
                    break;
                case LFOTriggerMode::MIDI:
                    // STUB: Will trigger on MIDI note-on when infrastructure ready
                    break;
                case LFOTriggerMode::Audio:
                    // STUB: Will trigger on audio transient when infrastructure ready
                    break;
            }

            if (shouldTrigger) {
                mod.phase = 0.0f;  // Reset to start
                mod.triggered = true;
            } else {
                mod.triggered = false;
            }

            // Calculate effective rate (Hz or tempo-synced)
            float effectiveRate = mod.rate;
            if (mod.tempoSync) {
                effectiveRate = ModulatorEngine::calculateSyncRateHz(mod.syncDivision, bpm);
            }

            // Update phase (wraps at 1.0)
            mod.phase += static_cast<float>(effectiveRate * deltaTime);
            while (mod.phase >= 1.0f) {
                mod.phase -= 1.0f;
            }
            // Apply phase offset when generating waveform
            float effectivePhase = std::fmod(mod.phase + mod.phaseOffset, 1.0f);
            // Generate waveform output using ModulatorEngine (handles Custom curves too)
            mod.value = ModulatorEngine::generateWaveformForMod(mod, effectivePhase);
        }
    };

    // Recursive lambda to update mods in chain elements
    std::function<void(ChainElement&)> updateElementMods = [&](ChainElement& element) {
        if (isDevice(element)) {
            // Update device mods
            DeviceInfo& device = magda::getDevice(element);
            for (auto& mod : device.mods) {
                updateMod(mod);
            }
        } else if (isRack(element)) {
            // Update rack mods
            RackInfo& rack = magda::getRack(element);
            for (auto& mod : rack.mods) {
                updateMod(mod);
            }
            // Recursively update mods in nested chains
            for (auto& chain : rack.chains) {
                for (auto& chainElement : chain.elements) {
                    updateElementMods(chainElement);
                }
            }
        }
    };

    // Update mods in all tracks
    for (auto& track : tracks_) {
        // Update mods in all chain elements
        for (auto& element : track.chainElements) {
            updateElementMods(element);
        }
    }

    // DO NOT call notifyModulationChanged() here - that causes 60 FPS UI rebuilds
    // ParamSlotComponent will read mod.value directly during its paint cycle
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
        device->macros[macroIndex].value = juce::jlimit(0.0f, 1.0f, value);
        // Don't notify - simple value change doesn't need UI rebuild
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
        }
        // Don't notify - simple value change doesn't need UI rebuild
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
        if (auto* link = device->macros[macroIndex].getLink(target)) {
            link->amount = amount;
        } else {
            // Link doesn't exist - create it
            MacroLink newLink;
            newLink.target = target;
            newLink.amount = amount;
            device->macros[macroIndex].links.push_back(newLink);
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
