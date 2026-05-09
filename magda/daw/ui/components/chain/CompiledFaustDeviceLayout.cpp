#include "CompiledFaustDeviceLayout.hpp"

#include <cmath>

namespace magda::daw::ui {

namespace {

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
    const int gateArrayIdx = findParamArrayIndex(device, param.gateSlotIndex);
    if (gateArrayIdx < 0)
        return true;
    const float gateValue = device.parameters[static_cast<size_t>(gateArrayIdx)].currentValue;
    const bool gateTruth = gateValue >= 0.5f;
    return param.gateNegated ? !gateTruth : gateTruth;
}

}  // namespace

int CompiledFaustDeviceLayout::totalPages(const magda::DeviceInfo&) const {
    return 1;
}

ParamCell CompiledFaustDeviceLayout::cellFor(const magda::DeviceInfo& device, int cellIndex,
                                             int) const {
    ParamCell cell;
    if (cellIndex < 0 || cellIndex >= cellCount_) {
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }

    const int paramArrayIdx = findParamArrayIndex(device, cellIndex);
    if (paramArrayIdx < 0) {
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }

    const auto& param = device.parameters[static_cast<size_t>(paramArrayIdx)];

    // A discrete cell whose plugin advertises ≤ 1 choice is functionally
    // inert (only one option to pick). Hide it instead of drawing a
    // meaningless dropdown — e.g. the filter's Mode slot when Engine =
    // Ladder, where modeChoicesForEngine() returns just {"LP"}.
    if (param.scale == magda::ParameterScale::Discrete && param.choices.size() <= 1) {
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }

    cell.mode = ParamCell::Mode::Filled;
    cell.paramArrayIndex = paramArrayIdx;
    cell.targetParamIndex = param.paramIndex >= 0 ? param.paramIndex : paramArrayIdx;
    cell.enabled = gateEnabled(device, param);
    return cell;
}

}  // namespace magda::daw::ui
