#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>

#include "magda/sdk/meter/MeterModel.hpp"

/// The frame clock the SDK meter model is advanced by.
namespace magda::level_meter_clock {

inline double restart() {
    return juce::Time::getMillisecondCounterHiRes();
}

/// Time since the last call; one nominal frame on the first.
inline float elapsedMs(double& lastUpdateMs) {
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = lastUpdateMs > 0.0 ? static_cast<float>(now - lastUpdateMs)
                                            : sdk::MeterBallistics::kNominalFrameMs;
    lastUpdateMs = now;
    return std::max(0.0f, elapsed);
}

}  // namespace magda::level_meter_clock
