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
        const int currentPage = paramGrid->getCurrentPage();
        const int pageOffset = currentPage * paramsPerPage;
        const bool useVisibilityFilter = !device.visibleParameters.empty();

        for (int slotIndex = 0; slotIndex < paramsPerPage; ++slotIndex) {
            const int visibleParamIndex = pageOffset + slotIndex;
            int actualParamIndex;
            if (useVisibilityFilter) {
                if (visibleParamIndex >= static_cast<int>(device.visibleParameters.size()))
                    continue;
                actualParamIndex = device.visibleParameters[static_cast<size_t>(visibleParamIndex)];
            } else {
                actualParamIndex = visibleParamIndex;
            }

            if (actualParamIndex == paramIndex) {
                if (auto* slot = paramGrid->getSlot(slotIndex))
                    slot->setParamValue(modelValue);
                break;
            }
        }
    }

    refreshDeviceSlotInlineUiParameterValues(device, compiledPanel, customUI);
}

}  // namespace magda::daw::ui
