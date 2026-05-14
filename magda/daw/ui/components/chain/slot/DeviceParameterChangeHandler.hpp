#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <unordered_map>

#include "core/TypeIds.hpp"

namespace magda {
struct DeviceInfo;
}

namespace magda::daw::ui {

class ParamHostComponent;

struct ParameterLearnHighlightState {
    int lockedParamIndex = -1;
    juce::uint32 lockTimeMs = 0;
    std::unordered_map<int, float> lastValueByParam;

    void reset();
};

void updateCachedParameterValue(magda::DeviceInfo& device, int paramIndex, float newValue);

bool refreshEngineAwareCompiledSlots(magda::DeviceInfo& device, magda::DeviceId deviceId,
                                     int changedParamIndex, ParamHostComponent& paramGrid);

void applyLearnModeParameterHighlight(magda::DeviceInfo& device, ParamHostComponent& paramGrid,
                                      int paramIndex, float newValue,
                                      ParameterLearnHighlightState& state,
                                      const std::function<void()>& onPageChanged);

void updateCurrentPageParameterSlotValue(const magda::DeviceInfo& device,
                                         ParamHostComponent& paramGrid, int paramIndex,
                                         float newValue);

}  // namespace magda::daw::ui
