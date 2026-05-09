#include "CompiledFaustDeviceLayout.hpp"

#include <algorithm>
#include <cmath>

#include "audio/plugins/compiled/MagdaFilterCompiledPlugin.hpp"

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
    if (cellIndex < 0 || cellIndex >= kCellCount) {
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }

    const int paramArrayIdx = findParamArrayIndex(device, cellIndex);
    if (paramArrayIdx < 0) {
        cell.mode = ParamCell::Mode::Hidden;
        return cell;
    }

    // Engine-aware visibility for the Mode slot: Ladder has no mode
    // picker (LP only), so hide the cell entirely when Engine is set to
    // Ladder. Slot indices come from MagdaFilterCompiledPlugin so a
    // reorder there flows through this layout automatically.
    using Filter = magda::daw::audio::compiled::MagdaFilterCompiledPlugin;
    if (cellIndex == Filter::kModeSlot) {
        const int engineArrayIdx = findParamArrayIndex(device, Filter::kEngineSlot);
        if (engineArrayIdx >= 0) {
            const float engineValue =
                device.parameters[static_cast<size_t>(engineArrayIdx)].currentValue;
            const int engineIdx = static_cast<int>(std::round(engineValue));
            if (engineIdx == static_cast<int>(Filter::FilterFamily::Ladder)) {
                cell.mode = ParamCell::Mode::Hidden;
                return cell;
            }
        }
    }

    const auto& param = device.parameters[static_cast<size_t>(paramArrayIdx)];
    cell.mode = ParamCell::Mode::Filled;
    cell.paramArrayIndex = paramArrayIdx;
    cell.targetParamIndex = param.paramIndex >= 0 ? param.paramIndex : paramArrayIdx;
    cell.enabled = gateEnabled(device, param);
    return cell;
}

}  // namespace magda::daw::ui
