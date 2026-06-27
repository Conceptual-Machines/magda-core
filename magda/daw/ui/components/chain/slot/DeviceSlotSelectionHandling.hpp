#pragma once

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/SelectionManager.hpp"

namespace magda {
class SvgButton;
}

namespace magda::daw::ui {

class MacroPanelComponent;
class ModsPanelComponent;
class ParamHostComponent;

struct DeviceSlotSelectionCallbacks {
    std::function<void()> updateParamModulation;
    std::function<void()> updateModsPanel;
    std::function<void()> updateMacroPanel;
    std::function<void(bool)> setParamPanelVisible;
};

void openDeviceSlotMacroPanelForSelectionIfNeeded(const magda::ChainNodePath& nodePath,
                                                  bool paramPanelVisible,
                                                  bool exposesDeviceModulation,
                                                  magda::SvgButton* macroButton,
                                                  const DeviceSlotSelectionCallbacks& callbacks);

void applyDeviceSlotSelectionTypeChange(magda::SelectionType newType, ParamHostComponent& paramGrid,
                                        const DeviceSlotSelectionCallbacks& callbacks);

void applyDeviceSlotModSelectionChange(const magda::ChainNodePath& nodePath,
                                       const magda::ModSelection& selection,
                                       ModsPanelComponent* modsPanel,
                                       const DeviceSlotSelectionCallbacks& callbacks);

void applyDeviceSlotMacroSelectionChange(const magda::ChainNodePath& nodePath,
                                         const magda::MacroSelection& selection,
                                         MacroPanelComponent* macroPanel,
                                         const DeviceSlotSelectionCallbacks& callbacks);

void applyDeviceSlotParamSelectionChange(const magda::ChainNodePath& nodePath,
                                         const magda::ParamSelection& selection,
                                         ParamHostComponent& paramGrid,
                                         const DeviceSlotSelectionCallbacks& callbacks);

}  // namespace magda::daw::ui
