#include "layout/StandardDeviceLayout.hpp"

#include <algorithm>

namespace magda::daw::ui {

namespace {

int visibleCountFor(const magda::DeviceInfo& device) {
    return device.visibleParameters.empty() ? static_cast<int>(device.parameters.size())
                                            : static_cast<int>(device.visibleParameters.size());
}

int paramArrayIndexFor(const magda::DeviceInfo& device, int slotIndex) {
    if (device.visibleParameters.empty())
        return slotIndex;
    if (slotIndex < 0 || slotIndex >= static_cast<int>(device.visibleParameters.size()))
        return -1;
    return device.visibleParameters[static_cast<size_t>(slotIndex)];
}

bool gateEnabled(const magda::DeviceInfo& device, const magda::ParameterInfo& param) {
    if (param.gateSlotIndex < 0)
        return true;
    if (param.gateSlotIndex >= static_cast<int>(device.parameters.size()))
        return true;
    const float gateValue =
        device.parameters[static_cast<size_t>(param.gateSlotIndex)].currentValue;
    const bool gateTruth = (gateValue >= 0.5f);
    // [gate:N]  → active when gate slot is ON
    // [gate:!N] → active when gate slot is OFF
    return param.gateNegated ? !gateTruth : gateTruth;
}

}  // namespace

int StandardDeviceLayout::totalPages(const magda::DeviceInfo& device) const {
    const int total = std::max(1, (visibleCountFor(device) + kCellCount - 1) / kCellCount);
    return total;
}

ParamCell StandardDeviceLayout::cellFor(const magda::DeviceInfo& device, int cellIndex,
                                        int currentPage) const {
    const int slotIndex = currentPage * kCellCount + cellIndex;
    const int visCount = visibleCountFor(device);

    ParamCell cell;
    if (slotIndex >= visCount) {
        cell.mode = ParamCell::Mode::Placeholder;
        return cell;
    }

    const int paramArrayIdx = paramArrayIndexFor(device, slotIndex);
    if (paramArrayIdx < 0 || paramArrayIdx >= static_cast<int>(device.parameters.size())) {
        cell.mode = ParamCell::Mode::Placeholder;
        return cell;
    }

    const auto& param = device.parameters[static_cast<size_t>(paramArrayIdx)];
    cell.mode = ParamCell::Mode::Filled;
    cell.paramArrayIndex = paramArrayIdx;
    cell.targetParamIndex = param.paramIndex >= 0 ? param.paramIndex : paramArrayIdx;
    cell.enabled = gateEnabled(device, param);
    return cell;
}

}  // namespace magda::daw::ui
