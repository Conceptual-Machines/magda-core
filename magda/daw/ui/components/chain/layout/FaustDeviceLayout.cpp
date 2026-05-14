#include "layout/FaustDeviceLayout.hpp"

#include <algorithm>

namespace magda::daw::ui {

namespace {

// Linear scan for the param whose pool slot index matches `poolIdx`.
// Faust devices typically have ≤32 active params per page, so a linear
// search is cheap; if that ever changes, this is the obvious place to
// swap in a lookup table.
int findParamArrayIndex(const magda::DeviceInfo& device, int poolIdx) {
    for (int k = 0; k < static_cast<int>(device.parameters.size()); ++k) {
        if (device.parameters[static_cast<size_t>(k)].paramIndex == poolIdx)
            return k;
    }
    return -1;
}

bool gateEnabled(const magda::DeviceInfo& device, const magda::ParameterInfo& param) {
    if (param.gateSlotIndex < 0)
        return true;
    // Faust gates target pool slot indices, not device.parameters[] positions —
    // resolve via the same lookup as cellFor.
    const int gateArrayIdx = findParamArrayIndex(device, param.gateSlotIndex);
    if (gateArrayIdx < 0)
        return true;  // gate target absent — leave control enabled
    const float gateValue = device.parameters[static_cast<size_t>(gateArrayIdx)].currentValue;
    const bool gateTruth = (gateValue >= 0.5f);
    return param.gateNegated ? !gateTruth : gateTruth;
}

int maxPoolIndex(const magda::DeviceInfo& device) {
    int maxIdx = -1;
    for (const auto& p : device.parameters)
        maxIdx = std::max(maxIdx, p.paramIndex);
    return maxIdx;
}

}  // namespace

int FaustDeviceLayout::totalPages(const magda::DeviceInfo& device) const {
    const int hi = maxPoolIndex(device);
    if (hi < 0)
        return 1;
    return std::max(1, (hi + 1 + kCellCount - 1) / kCellCount);
}

ParamCell FaustDeviceLayout::cellFor(const magda::DeviceInfo& device, int cellIndex,
                                     int currentPage) const {
    const int targetPoolIdx = currentPage * kCellCount + cellIndex;
    const int paramArrayIdx = findParamArrayIndex(device, targetPoolIdx);

    ParamCell cell;
    if (paramArrayIdx < 0) {
        // No param at this pool slot — the cell is truly empty.
        cell.mode = ParamCell::Mode::Hidden;
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
