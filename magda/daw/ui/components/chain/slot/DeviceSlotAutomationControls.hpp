#pragma once

#include "core/AutomationManager.hpp"
#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"
#include "slot/DeviceCustomUIManager.hpp"

namespace magda::daw::ui {

class CompiledDevicePanel;
class ParamHostComponent;

void showDeviceSlotAutomationLaneForParam(const magda::ChainNodePath& nodePath, int paramIndex);

void applyDeviceSlotAutomationValueChange(magda::DeviceInfo& device, ParamHostComponent* paramGrid,
                                          CompiledDevicePanel* compiledPanel,
                                          DeviceCustomUIManager& customUI,
                                          magda::AutomationLaneId laneId, double normalizedValue);

}  // namespace magda::daw::ui
