#pragma once

#include <functional>

#include "core/ChainNodePath.hpp"
#include "core/DeviceInfo.hpp"

namespace magda::daw::ui {

class CompiledDevicePanel;
class ParamHostComponent;
struct DeviceSlotTraits;

struct DeviceSlotParameterPagingCallbacks {
    std::function<void()> reloadParameterSlots;
    std::function<void()> updateParamModulation;
    std::function<void()> repaint;
};

void updateDeviceSlotParameterSlots(magda::DeviceInfo& device, const magda::ChainNodePath& nodePath,
                                    ParamHostComponent& paramGrid,
                                    CompiledDevicePanel* compiledPanel,
                                    const DeviceSlotTraits& traits,
                                    DeviceSlotParameterPagingCallbacks callbacks);

void updateDeviceSlotParameterValues(const magda::DeviceInfo& device,
                                     ParamHostComponent& paramGrid);

bool applyDeviceSlotSavedParameterConfig(magda::DeviceInfo& device,
                                         const magda::ChainNodePath& nodePath,
                                         ParamHostComponent* paramGrid);

void updateDeviceSlotParameterPagination(const magda::DeviceInfo& device,
                                         ParamHostComponent* paramGrid);

void goToPreviousDeviceSlotParameterPage(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                         DeviceSlotParameterPagingCallbacks callbacks);

void goToNextDeviceSlotParameterPage(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                     DeviceSlotParameterPagingCallbacks callbacks);

}  // namespace magda::daw::ui
