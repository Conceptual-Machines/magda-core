#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <optional>

#include "../../core/ClipTypes.hpp"

namespace magda::clip_launch {

tracktion::LaunchQType toTracktionLaunchQType(LaunchQuantize quantize);

std::optional<tracktion::MonotonicBeat> computeQuantizedBeat(tracktion::Edit& edit,
                                                             LaunchQuantize quantize);

std::optional<double> toEditTimeSeconds(tracktion::Edit& edit,
                                        tracktion::MonotonicBeat monotonicBeat);

}  // namespace magda::clip_launch
