#include "slot/DeviceSlotParameterPaging.hpp"

#include <utility>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/TrackManager.hpp"
#include "params/ParamHostComponent.hpp"
#include "slot/DeviceParameterChangeHandler.hpp"
#include "slot/DeviceSlotTraits.hpp"
#include "ui/dialogs/ParameterConfigDialog.hpp"

namespace magda::daw::ui {

namespace {

void reloadPage(DeviceSlotParameterPagingCallbacks callbacks) {
    if (callbacks.reloadParameterSlots)
        callbacks.reloadParameterSlots();
    if (callbacks.updateParamModulation)
        callbacks.updateParamModulation();
    if (callbacks.repaint)
        callbacks.repaint();
}

}  // namespace

void updateDeviceSlotParameterSlots(magda::DeviceInfo& device, const magda::ChainNodePath& nodePath,
                                    ParamHostComponent& paramGrid,
                                    CompiledDevicePanel* compiledPanel,
                                    const DeviceSlotTraits& traits,
                                    DeviceSlotParameterPagingCallbacks callbacks) {
    paramGrid.updateParameterSlots(
        device, paramGrid.getCurrentPage(), [&](int paramIndex, double value) {
            if (!nodePath.isValid())
                return;

            if (auto* param = device.findParameterByIndex(paramIndex))
                param->currentValue = static_cast<float>(value);
            if (compiledPanel != nullptr)
                compiledPanel->updateFromDevice(device);

            magda::TrackManager::getInstance().setDeviceParameterValue(nodePath, paramIndex,
                                                                       static_cast<float>(value));
            if (traits.compiledPresentation &&
                refreshEngineAwareCompiledSlots(device, nodePath, paramIndex, paramGrid)) {
                if (callbacks.reloadParameterSlots)
                    callbacks.reloadParameterSlots();
                if (callbacks.updateParamModulation)
                    callbacks.updateParamModulation();
                return;
            }

            paramGrid.refreshEnabledStates(device, paramGrid.getCurrentPage());
        });
}

void updateDeviceSlotParameterValues(const magda::DeviceInfo& device,
                                     ParamHostComponent& paramGrid) {
    paramGrid.updateParameterValues(device, paramGrid.getCurrentPage());
}

bool applyDeviceSlotSavedParameterConfig(magda::DeviceInfo& device,
                                         const magda::ChainNodePath& nodePath,
                                         ParamHostComponent* paramGrid) {
    if (paramGrid == nullptr || device.uniqueId.isEmpty() || device.parameters.empty())
        return false;

    magda::DeviceInfo tempDevice = device;
    if (!ParameterConfigDialog::applyConfigToDevice(tempDevice.uniqueId, tempDevice))
        return false;

    if (!tempDevice.visibleParameters.empty()) {
        if (nodePath.isValid())
            magda::TrackManager::getInstance().setDeviceVisibleParameters(
                nodePath, tempDevice.visibleParameters);
        device.visibleParameters = tempDevice.visibleParameters;
    }

    if (nodePath.isValid())
        magda::TrackManager::getInstance().setDeviceMiniMixerParameters(
            nodePath, tempDevice.miniMixerParameters);
    device.miniMixerParameters = tempDevice.miniMixerParameters;

    device.parameters = tempDevice.parameters;
    return true;
}

void updateDeviceSlotParameterPagination(const magda::DeviceInfo& device,
                                         ParamHostComponent* paramGrid) {
    if (paramGrid == nullptr)
        return;

    const int totalPages = juce::jmax(1, paramGrid->getLayout().totalPages(device));
    int currentPage = device.currentParameterPage;
    if (currentPage >= totalPages)
        currentPage = totalPages - 1;
    if (currentPage < 0)
        currentPage = 0;
    paramGrid->updatePageControls(currentPage, totalPages);
}

void goToPreviousDeviceSlotParameterPage(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                         DeviceSlotParameterPagingCallbacks callbacks) {
    const int currentPage = paramGrid.getCurrentPage();
    if (currentPage <= 0)
        return;

    const int newPage = currentPage - 1;
    device.currentParameterPage = newPage;
    paramGrid.updatePageControls(newPage, paramGrid.getTotalPages());
    reloadPage(std::move(callbacks));
}

void goToNextDeviceSlotParameterPage(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                     DeviceSlotParameterPagingCallbacks callbacks) {
    const int currentPage = paramGrid.getCurrentPage();
    const int totalPages = paramGrid.getTotalPages();
    if (currentPage >= totalPages - 1)
        return;

    const int newPage = currentPage + 1;
    device.currentParameterPage = newPage;
    paramGrid.updatePageControls(newPage, totalPages);
    reloadPage(std::move(callbacks));
}

}  // namespace magda::daw::ui
