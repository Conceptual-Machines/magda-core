#include "slot/DeviceSlotAutomationControls.hpp"

#include "core/ParameterUtils.hpp"
#include "params/ParamHostComponent.hpp"
#include "params/ParamSlotComponent.hpp"
#include "slot/DeviceSlotInlineUiFactory.hpp"

namespace magda::daw::ui {

void showDeviceSlotAutomationLaneForParam(const magda::ChainNodePath& nodePath, int paramIndex) {
    if (nodePath.isPostFx())
        return;

    auto trackId = nodePath.trackId;
    if (trackId == magda::INVALID_TRACK_ID)
        return;

    magda::AutomationTarget target;
    target.kind = magda::ControlTarget::Kind::PluginParam;
    target.devicePath.trackId = trackId;
    target.devicePath = nodePath;
    target.paramIndex = paramIndex;

    auto& automationMgr = magda::AutomationManager::getInstance();
    auto laneId = automationMgr.getOrCreateLane(target, magda::AutomationLaneType::Absolute);
    automationMgr.setLaneVisible(laneId, true);
}

void applyDeviceSlotAutomationValueChange(magda::DeviceInfo& device, ParamHostComponent* paramGrid,
                                          CompiledDevicePanel* compiledPanel,
                                          DeviceCustomUIManager& customUI,
                                          magda::AutomationLaneId laneId, double normalizedValue) {
    // Curve-driven update: the lane has pushed a new value (drag preview,
    // stopped rebake, or TE playback). Only react to DeviceParameter lanes
    // that target this device; lane registration is global.
    const auto* lane = magda::AutomationManager::getInstance().getLane(laneId);
    if (!lane)
        return;

    if (lane->target.kind != magda::ControlTarget::Kind::PluginParam)
        return;

    if (lane->target.devicePath.getDeviceId() != device.id)
        return;

    // Overridden state covers both "user dragging right now" and "user
    // released and the lane is latched to their value"; either way, skip the
    // curve write so we don't yank the control back to the curve.
    if (magda::AutomationManager::getInstance().getVisualState(lane->target) ==
        magda::AutomationVisualState::Overridden)
        return;

    const int paramIndex = lane->target.paramIndex;
    auto* stored = device.findParameterByIndex(paramIndex);
    if (stored == nullptr)
        return;

    const float modelValue =
        magda::ParameterUtils::normalizedToModelValue(
            magda::ParameterNormalizedValue::clamped(static_cast<float>(normalizedValue)), *stored)
            .value;

    // Keep the cached value in sync so non-automation refresh paths and custom
    // UI read the same value-space that live parameter writes use.
    stored->currentValue = modelValue;

    if (paramGrid != nullptr) {
        const int paramsPerPage = paramGrid->getSlotCount();
        const int pageOffset = paramGrid->getCurrentPage() * paramsPerPage;

        // Which slot the grid draws in each cell: the user's selection where
        // there is one (#2638), the array in order otherwise.
        const auto slotInCell = [&device](int cell) {
            const auto at = [](const auto& list, int index) {
                return index >= 0 && index < static_cast<int>(list.size());
            };

            if (!device.visibleParameters.empty())
                return at(device.visibleParameters, cell)
                           ? device.visibleParameters[static_cast<size_t>(cell)]
                           : -1;

            return at(device.parameters, cell)
                       ? device.parameters[static_cast<size_t>(cell)].paramIndex
                       : -1;
        };

        for (int slotIndex = 0; slotIndex < paramsPerPage; ++slotIndex) {
            if (slotInCell(pageOffset + slotIndex) != paramIndex)
                continue;

            if (auto* slot = paramGrid->getSlot(slotIndex))
                slot->setParamValue(modelValue);
            break;
        }
    }

    refreshDeviceSlotInlineUiParameterValues(device, compiledPanel, customUI);
}

}  // namespace magda::daw::ui
