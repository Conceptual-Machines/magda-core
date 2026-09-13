#include "slot/DeviceSlotParameterPaging.hpp"

#include <algorithm>
#include <utility>

#include "compiled/CompiledPluginPresentation.hpp"
#include "core/ParameterUtils.hpp"
#include "core/TrackManager.hpp"
#include "params/ParamHostComponent.hpp"
#include "slot/DeviceParameterChangeHandler.hpp"
#include "slot/DeviceSlotTraits.hpp"

namespace magda::daw::ui {

namespace {

void reloadPage(const DeviceSlotParameterPagingCallbacks& callbacks) {
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
                                    const DeviceSlotParameterPagingCallbacks& callbacks) {
    // Each parameter slot stores a copy of this callback and invokes it on a
    // later mouse drag, so it must NOT capture the function-local `compiledPanel`
    // pointer / `callbacks` struct by reference - those die when this function
    // returns, and the dangling read crashes on the next edit. Capture them by
    // value; the reference parameters are bound to DeviceSlotComponent members,
    // which outlive the slots.
    paramGrid.updateParameterSlots(
        device, paramGrid.getCurrentPage(),
        [&device, &nodePath, &paramGrid, &traits, compiledPanel, callbacks](int paramIndex,
                                                                            double value) {
            if (!nodePath.isValid())
                return;

            // The grid works in display units and this list is in model ones,
            // which differ wherever a parameter carries a configured range.
            auto* param = device.findParameterByIndex(paramIndex);
            const auto model =
                param != nullptr
                    ? magda::ParameterUtils::realToModelValue(static_cast<float>(value), *param)
                    : magda::ParameterModelValue{static_cast<float>(value)};
            if (param != nullptr)
                param->currentValue = model.value;
            if (compiledPanel != nullptr)
                compiledPanel->updateFromDevice(device);

            // Described from this slot's own list, which holds every parameter
            // the plugin has: the model holds only the ones a host control
            // drives, and the rest are the plugin's to be told
            // (docs/specs/hosted-plugin-parameter-control.md).
            if (param != nullptr)
                magda::TrackManager::getInstance().setDeviceParameterValue(nodePath, *param, model);
            else
                magda::TrackManager::getInstance().setDeviceParameterValue(
                    nodePath, paramIndex, static_cast<float>(value));
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

void updateDeviceSlotParameterPagination(const magda::DeviceInfo& device,
                                         ParamHostComponent* paramGrid) {
    if (paramGrid == nullptr)
        return;

    const int totalPages = juce::jmax(1, paramGrid->getLayout().totalPages(device));
    int currentPage = device.currentParameterPage;
    if (currentPage >= totalPages)
        currentPage = totalPages - 1;
    currentPage = std::max(currentPage, 0);
    paramGrid->updatePageControls(device, currentPage, totalPages);
}

void goToPreviousDeviceSlotParameterPage(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                         DeviceSlotParameterPagingCallbacks callbacks) {
    const int currentPage = paramGrid.getCurrentPage();
    if (currentPage <= 0)
        return;

    const int newPage = currentPage - 1;
    device.currentParameterPage = newPage;
    paramGrid.updatePageControls(device, newPage, paramGrid.getTotalPages());
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
    paramGrid.updatePageControls(device, newPage, totalPages);
    reloadPage(std::move(callbacks));
}

void goToDeviceSlotParameterPage(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                 int pageIndex, DeviceSlotParameterPagingCallbacks callbacks) {
    const int totalPages = paramGrid.getTotalPages();
    if (pageIndex < 0 || pageIndex >= totalPages || pageIndex == paramGrid.getCurrentPage())
        return;

    device.currentParameterPage = pageIndex;
    paramGrid.updatePageControls(device, pageIndex, totalPages);
    reloadPage(std::move(callbacks));
}

}  // namespace magda::daw::ui
