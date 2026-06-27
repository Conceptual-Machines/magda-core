#include "slot/DeviceSlotModMacroCommands.hpp"

#include "core/LinkModeManager.hpp"
#include "core/SelectionManager.hpp"
#include "core/TrackCommands.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

namespace {

void updateParamModulation(const DeviceSlotModMacroCommandCallbacks& callbacks) {
    if (callbacks.updateParamModulation)
        callbacks.updateParamModulation();
}

void updateModsPanel(const DeviceSlotModMacroCommandCallbacks& callbacks) {
    if (callbacks.updateModsPanel)
        callbacks.updateModsPanel();
}

void updateMacroPanel(const DeviceSlotModMacroCommandCallbacks& callbacks) {
    if (callbacks.updateMacroPanel)
        callbacks.updateMacroPanel();
}

void refreshPanels(const DeviceSlotModMacroCommandCallbacks& callbacks) {
    if (callbacks.refreshPanels)
        callbacks.refreshPanels();
}

}  // namespace

void setDeviceSlotModTarget(const magda::ChainNodePath& nodePath, int modIndex,
                            magda::ControlTarget target) {
    magda::TrackManager::getInstance().setModTarget(nodePath, modIndex, target);
}

void renameDeviceSlotMod(const magda::ChainNodePath& nodePath, int modIndex,
                         const juce::String& name) {
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetModNameCommand>(nodePath, modIndex, name));
}

void setDeviceSlotModType(const magda::ChainNodePath& nodePath, int modIndex, magda::ModType type) {
    magda::TrackManager::getInstance().setModType(nodePath, modIndex, type);
}

void setDeviceSlotModWaveform(const magda::ChainNodePath& nodePath, int modIndex,
                              magda::LFOWaveform waveform) {
    magda::TrackManager::getInstance().setModWaveform(nodePath, modIndex, waveform);
}

void setDeviceSlotModRate(const magda::ChainNodePath& nodePath, int modIndex, float rate) {
    magda::TrackManager::getInstance().setModRate(nodePath, modIndex, rate);
}

void setDeviceSlotModPhaseOffset(const magda::ChainNodePath& nodePath, int modIndex,
                                 float phaseOffset) {
    magda::TrackManager::getInstance().setModPhaseOffset(nodePath, modIndex, phaseOffset);
}

void setDeviceSlotModTempoSync(const magda::ChainNodePath& nodePath, int modIndex, bool tempoSync) {
    magda::TrackManager::getInstance().setModTempoSync(nodePath, modIndex, tempoSync);
}

void setDeviceSlotModSyncDivision(const magda::ChainNodePath& nodePath, int modIndex,
                                  magda::SyncDivision division) {
    magda::TrackManager::getInstance().setModSyncDivision(nodePath, modIndex, division);
}

void setDeviceSlotModTriggerMode(const magda::ChainNodePath& nodePath, int modIndex,
                                 magda::LFOTriggerMode mode) {
    magda::TrackManager::getInstance().setModTriggerMode(nodePath, modIndex, mode);
}

void setDeviceSlotModAudioAttack(const magda::ChainNodePath& nodePath, int modIndex, float ms) {
    magda::TrackManager::getInstance().setModAudioAttack(nodePath, modIndex, ms);
}

void setDeviceSlotModAudioRelease(const magda::ChainNodePath& nodePath, int modIndex, float ms) {
    magda::TrackManager::getInstance().setModAudioRelease(nodePath, modIndex, ms);
}

void setDeviceSlotModEnvelope(const magda::ChainNodePath& nodePath, int modIndex,
                              const magda::ModInfo& mod) {
    magda::TrackManager::getInstance().setModEnvelope(nodePath, modIndex, mod);
}

void setDeviceSlotModRandom(const magda::ChainNodePath& nodePath, int modIndex,
                            const magda::ModInfo& mod) {
    magda::TrackManager::getInstance().setModRandom(nodePath, modIndex, mod);
}

void setDeviceSlotModFollower(const magda::ChainNodePath& nodePath, int modIndex,
                              const magda::ModInfo& mod) {
    magda::TrackManager::getInstance().setModFollower(nodePath, modIndex, mod);
}

void notifyDeviceSlotModCurveChanged(const magda::ChainNodePath& nodePath) {
    DBG("[HardCorner] DeviceSlotComponent notifyModCurveChanged path=" << nodePath.toString());
    // Curve points are already written directly to ModInfo by LFOCurveEditor.
    // Just notify the audio thread to pick up the new data.
    magda::TrackManager::getInstance().notifyModCurveChanged(nodePath);
}

void setDeviceSlotMacroValue(const magda::ChainNodePath& nodePath, int macroIndex, float value,
                             const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setMacroValue(nodePath, macroIndex, value);
    updateParamModulation(callbacks);
}

void setDeviceSlotMacroTarget(const magda::ChainNodePath& nodePath, int macroIndex,
                              magda::ControlTarget target,
                              const DeviceSlotModMacroCommandCallbacks& callbacks) {
    // Check if the active macro is from this device or a parent rack.
    auto activeMacroSelection = magda::LinkModeManager::getInstance().getMacroInLinkMode();
    if (activeMacroSelection.isValid() && activeMacroSelection.parentPath == nodePath) {
        magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
    } else if (activeMacroSelection.isValid()) {
        magda::TrackManager::getInstance().setMacroTarget(activeMacroSelection.parentPath,
                                                          macroIndex, target);
    } else {
        magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
    }
    updateParamModulation(callbacks);
}

void renameDeviceSlotMacro(const magda::ChainNodePath& nodePath, int macroIndex,
                           const juce::String& name) {
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetMacroNameCommand>(nodePath, macroIndex, name));
}

void clearAllDeviceSlotMacroLinks(const magda::ChainNodePath& nodePath, int macroIndex,
                                  const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().clearAllMacroLinks(nodePath, macroIndex);
    updateParamModulation(callbacks);
    updateMacroPanel(callbacks);
}

void setDeviceSlotMacroLinkAmount(const magda::ChainNodePath& nodePath, int macroIndex,
                                  magda::ControlTarget target, float amount,
                                  const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target, amount);
    updateParamModulation(callbacks);
}

void createDeviceSlotMacroLink(const magda::ChainNodePath& nodePath, int macroIndex,
                               magda::ControlTarget target, float amount,
                               const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setMacroTarget(nodePath, macroIndex, target);
    magda::TrackManager::getInstance().setMacroLinkAmount(nodePath, macroIndex, target, amount);
    updateParamModulation(callbacks);

    if (target.isValid())
        magda::SelectionManager::getInstance().selectParam(nodePath, target.paramIndex);
}

void removeDeviceSlotMacroLink(const magda::ChainNodePath& nodePath, int macroIndex,
                               magda::ControlTarget target,
                               const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().removeMacroLink(nodePath, macroIndex, target);
    updateMacroPanel(callbacks);
    updateParamModulation(callbacks);
}

void setDeviceSlotMacroLinkBipolar(const magda::ChainNodePath& nodePath, int macroIndex,
                                   magda::ControlTarget target, bool bipolar,
                                   const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setMacroLinkBipolar(nodePath, macroIndex, target, bipolar);
    updateParamModulation(callbacks);
}

void selectDeviceSlotMod(const magda::ChainNodePath& nodePath, int modIndex) {
    magda::SelectionManager::getInstance().selectMod(nodePath, modIndex);
}

void selectDeviceSlotMacro(const magda::ChainNodePath& nodePath, int macroIndex) {
    magda::SelectionManager::getInstance().selectMacro(nodePath, macroIndex);
}

void setDeviceSlotModLinkAmount(const magda::ChainNodePath& nodePath, int modIndex,
                                magda::ControlTarget target, float amount,
                                const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target, amount);
    updateParamModulation(callbacks);
}

void setDeviceSlotModLinkEnabled(const magda::ChainNodePath& nodePath, int modIndex,
                                 magda::ControlTarget target, bool enabled,
                                 const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setModLinkEnabled(nodePath, modIndex, target, enabled);
    updateParamModulation(callbacks);
}

void createDeviceSlotModLink(const magda::ChainNodePath& nodePath, int modIndex,
                             magda::ControlTarget target, float amount,
                             const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().setModTarget(nodePath, modIndex, target);
    magda::TrackManager::getInstance().setModLinkAmount(nodePath, modIndex, target, amount);
    updateParamModulation(callbacks);

    if (target.isValid())
        magda::SelectionManager::getInstance().selectParam(nodePath, target.paramIndex);
}

void removeDeviceSlotModLink(const magda::ChainNodePath& nodePath, int modIndex,
                             magda::ControlTarget target,
                             const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().removeModLink(nodePath, modIndex, target);
    updateModsPanel(callbacks);
    updateParamModulation(callbacks);
}

void clearAllDeviceSlotModLinks(const magda::ChainNodePath& nodePath, int modIndex,
                                const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().clearAllModLinks(nodePath, modIndex);
    updateModsPanel(callbacks);
    updateParamModulation(callbacks);
}

void addDeviceSlotMod(const magda::ChainNodePath& nodePath, int slotIndex, magda::ModType type,
                      magda::LFOWaveform waveform,
                      const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().addMod(nodePath, slotIndex, type, waveform);
    // Adding a mod can introduce a new ModParam target for macros, so both
    // panels need a guarded refresh.
    refreshPanels(callbacks);
}

void removeDeviceSlotMod(const magda::ChainNodePath& nodePath, int modIndex,
                         const DeviceSlotModMacroCommandCallbacks& callbacks) {
    magda::TrackManager::getInstance().removeMod(nodePath, modIndex);
    refreshPanels(callbacks);
}

void setDeviceSlotModEnabled(const magda::ChainNodePath& nodePath, int modIndex, bool enabled) {
    magda::TrackManager::getInstance().setModEnabled(nodePath, modIndex, enabled);
}

void addDeviceSlotMacroPage(const magda::ChainNodePath& nodePath) {
    magda::TrackManager::getInstance().addMacroPage(nodePath);
}

void removeDeviceSlotMacroPage(const magda::ChainNodePath& nodePath) {
    magda::TrackManager::getInstance().removeMacroPage(nodePath);
}

}  // namespace magda::daw::ui
