#include "slot/DeviceSlotSelectionHandling.hpp"

#include "core/Config.hpp"
#include "modulation/MacroPanelComponent.hpp"
#include "modulation/ModsPanelComponent.hpp"
#include "params/ParamHostComponent.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

namespace {

void updateParamModulation(const DeviceSlotSelectionCallbacks& callbacks) {
    if (callbacks.updateParamModulation)
        callbacks.updateParamModulation();
}

void updateModsPanel(const DeviceSlotSelectionCallbacks& callbacks) {
    if (callbacks.updateModsPanel)
        callbacks.updateModsPanel();
}

void updateMacroPanel(const DeviceSlotSelectionCallbacks& callbacks) {
    if (callbacks.updateMacroPanel)
        callbacks.updateMacroPanel();
}

}  // namespace

void openDeviceSlotMacroPanelForSelectionIfNeeded(const magda::ChainNodePath& nodePath,
                                                  bool paramPanelVisible,
                                                  bool exposesDeviceModulation,
                                                  magda::SvgButton* macroButton,
                                                  const DeviceSlotSelectionCallbacks& callbacks) {
    if (!magda::Config::getInstance().getOpenMacrosOnSelect() || paramPanelVisible ||
        macroButton == nullptr || !nodePath.isValid() || !exposesDeviceModulation) {
        return;
    }

    const auto& selectedPath = magda::SelectionManager::getInstance().getSelectedChainNode();
    if (selectedPath != nodePath)
        return;

    macroButton->setToggleState(true, juce::dontSendNotification);
    macroButton->setActive(true);
    if (callbacks.setParamPanelVisible)
        callbacks.setParamPanelVisible(true);
}

void applyDeviceSlotSelectionTypeChange(magda::SelectionType newType, ParamHostComponent& paramGrid,
                                        const DeviceSlotSelectionCallbacks& callbacks) {
    if (newType != magda::SelectionType::Param)
        paramGrid.setAllSlotsSelected(false);

    updateParamModulation(callbacks);
}

void applyDeviceSlotModSelectionChange(const magda::ChainNodePath& nodePath,
                                       const magda::ModSelection& selection,
                                       ModsPanelComponent* modsPanel,
                                       const DeviceSlotSelectionCallbacks& callbacks) {
    updateParamModulation(callbacks);

    if (modsPanel == nullptr)
        return;

    if (selection.isValid() && selection.parentPath == nodePath) {
        modsPanel->setSelectedModIndex(selection.modIndex);
    } else {
        modsPanel->setSelectedModIndex(-1);
    }
}

void applyDeviceSlotMacroSelectionChange(const magda::ChainNodePath& nodePath,
                                         const magda::MacroSelection& selection,
                                         MacroPanelComponent* macroPanel,
                                         const DeviceSlotSelectionCallbacks& callbacks) {
    updateParamModulation(callbacks);

    if (macroPanel == nullptr)
        return;

    if (selection.isValid() && selection.parentPath == nodePath) {
        macroPanel->setSelectedMacroIndex(selection.macroIndex);
    } else {
        macroPanel->setSelectedMacroIndex(-1);
    }
}

void applyDeviceSlotParamSelectionChange(const magda::ChainNodePath& nodePath,
                                         const magda::ParamSelection& selection,
                                         ParamHostComponent& paramGrid,
                                         const DeviceSlotSelectionCallbacks& callbacks) {
    // Refresh mod and macro data from TrackManager before setting selected param.
    // This keeps knob amount displays in sync with fresh link data.
    updateModsPanel(callbacks);
    updateMacroPanel(callbacks);

    for (int i = 0; i < paramGrid.getSlotCount(); ++i) {
        const bool isSelected =
            selection.isValid() && selection.devicePath == nodePath && selection.paramIndex == i;
        paramGrid.setSlotSelected(i, isSelected);
    }
}

}  // namespace magda::daw::ui
