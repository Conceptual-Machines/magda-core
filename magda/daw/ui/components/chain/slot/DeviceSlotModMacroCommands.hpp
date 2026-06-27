#pragma once

#include <juce_core/juce_core.h>

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/ControlTarget.hpp"
#include "core/ModInfo.hpp"

namespace magda::daw::ui {

struct DeviceSlotModMacroCommandCallbacks {
    std::function<void()> updateParamModulation;
    std::function<void()> updateModsPanel;
    std::function<void()> updateMacroPanel;
    std::function<void()> refreshPanels;
};

void setDeviceSlotModTarget(const magda::ChainNodePath& nodePath, int modIndex,
                            magda::ControlTarget target);
void renameDeviceSlotMod(const magda::ChainNodePath& nodePath, int modIndex,
                         const juce::String& name);
void setDeviceSlotModType(const magda::ChainNodePath& nodePath, int modIndex, magda::ModType type);
void setDeviceSlotModWaveform(const magda::ChainNodePath& nodePath, int modIndex,
                              magda::LFOWaveform waveform);
void setDeviceSlotModRate(const magda::ChainNodePath& nodePath, int modIndex, float rate);
void setDeviceSlotModPhaseOffset(const magda::ChainNodePath& nodePath, int modIndex,
                                 float phaseOffset);
void setDeviceSlotModTempoSync(const magda::ChainNodePath& nodePath, int modIndex, bool tempoSync);
void setDeviceSlotModSyncDivision(const magda::ChainNodePath& nodePath, int modIndex,
                                  magda::SyncDivision division);
void setDeviceSlotModTriggerMode(const magda::ChainNodePath& nodePath, int modIndex,
                                 magda::LFOTriggerMode mode);
void setDeviceSlotModAudioAttack(const magda::ChainNodePath& nodePath, int modIndex, float ms);
void setDeviceSlotModAudioRelease(const magda::ChainNodePath& nodePath, int modIndex, float ms);
void setDeviceSlotModEnvelope(const magda::ChainNodePath& nodePath, int modIndex,
                              const magda::ModInfo& mod);
void setDeviceSlotModRandom(const magda::ChainNodePath& nodePath, int modIndex,
                            const magda::ModInfo& mod);
void setDeviceSlotModFollower(const magda::ChainNodePath& nodePath, int modIndex,
                              const magda::ModInfo& mod);
void notifyDeviceSlotModCurveChanged(const magda::ChainNodePath& nodePath);

void setDeviceSlotMacroValue(const magda::ChainNodePath& nodePath, int macroIndex, float value,
                             const DeviceSlotModMacroCommandCallbacks& callbacks);
void setDeviceSlotMacroTarget(const magda::ChainNodePath& nodePath, int macroIndex,
                              magda::ControlTarget target,
                              const DeviceSlotModMacroCommandCallbacks& callbacks);
void renameDeviceSlotMacro(const magda::ChainNodePath& nodePath, int macroIndex,
                           const juce::String& name);
void clearAllDeviceSlotMacroLinks(const magda::ChainNodePath& nodePath, int macroIndex,
                                  const DeviceSlotModMacroCommandCallbacks& callbacks);
void setDeviceSlotMacroLinkAmount(const magda::ChainNodePath& nodePath, int macroIndex,
                                  magda::ControlTarget target, float amount,
                                  const DeviceSlotModMacroCommandCallbacks& callbacks);
void createDeviceSlotMacroLink(const magda::ChainNodePath& nodePath, int macroIndex,
                               magda::ControlTarget target, float amount,
                               const DeviceSlotModMacroCommandCallbacks& callbacks);
void removeDeviceSlotMacroLink(const magda::ChainNodePath& nodePath, int macroIndex,
                               magda::ControlTarget target,
                               const DeviceSlotModMacroCommandCallbacks& callbacks);
void setDeviceSlotMacroLinkBipolar(const magda::ChainNodePath& nodePath, int macroIndex,
                                   magda::ControlTarget target, bool bipolar,
                                   const DeviceSlotModMacroCommandCallbacks& callbacks);

void selectDeviceSlotMod(const magda::ChainNodePath& nodePath, int modIndex);
void selectDeviceSlotMacro(const magda::ChainNodePath& nodePath, int macroIndex);

void setDeviceSlotModLinkAmount(const magda::ChainNodePath& nodePath, int modIndex,
                                magda::ControlTarget target, float amount,
                                const DeviceSlotModMacroCommandCallbacks& callbacks);
void setDeviceSlotModLinkEnabled(const magda::ChainNodePath& nodePath, int modIndex,
                                 magda::ControlTarget target, bool enabled,
                                 const DeviceSlotModMacroCommandCallbacks& callbacks);
void createDeviceSlotModLink(const magda::ChainNodePath& nodePath, int modIndex,
                             magda::ControlTarget target, float amount,
                             const DeviceSlotModMacroCommandCallbacks& callbacks);
void removeDeviceSlotModLink(const magda::ChainNodePath& nodePath, int modIndex,
                             magda::ControlTarget target,
                             const DeviceSlotModMacroCommandCallbacks& callbacks);
void clearAllDeviceSlotModLinks(const magda::ChainNodePath& nodePath, int modIndex,
                                const DeviceSlotModMacroCommandCallbacks& callbacks);
void addDeviceSlotMod(const magda::ChainNodePath& nodePath, int slotIndex, magda::ModType type,
                      magda::LFOWaveform waveform,
                      const DeviceSlotModMacroCommandCallbacks& callbacks);
void removeDeviceSlotMod(const magda::ChainNodePath& nodePath, int modIndex,
                         const DeviceSlotModMacroCommandCallbacks& callbacks);
void setDeviceSlotModEnabled(const magda::ChainNodePath& nodePath, int modIndex, bool enabled);

void addDeviceSlotMacroPage(const magda::ChainNodePath& nodePath);
void removeDeviceSlotMacroPage(const magda::ChainNodePath& nodePath);

}  // namespace magda::daw::ui
