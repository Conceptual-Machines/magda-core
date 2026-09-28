#pragma once

#include <cstddef>
#include <vector>

#include "AutomationInfo.hpp"

namespace magda::tempo_lane {

/** @brief Normalised lane value (0-1) for a BPM, through the Tempo parameter's range. */
double bpmToNormalized(double bpm);

/** @brief BPM for a normalised lane value (0-1). */
double normalizedToBpm(double normalized);

/** @brief Apex fraction of a segment whose ramp has this tension. */
double curvedTFromTension(double tension);

/**
 * @brief Tension in [-1, 1] of the ramp from points[i] to points[i + 1].
 *
 * A tempo ramp has one bend per segment, so the lane's bezier apex is projected
 * onto it; its horizontal offset and any asymmetry are dropped. `points` must be
 * sorted by beat.
 */
float segmentTension(const std::vector<AutomationPoint>& points, std::size_t i);

}  // namespace magda::tempo_lane
