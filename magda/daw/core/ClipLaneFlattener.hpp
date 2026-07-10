#pragma once

#include <functional>
#include <vector>

#include "AutomationInfo.hpp"

namespace magda {

/**
 * @brief Flatten a clip-based lane into an absolute-style breakpoint list
 *        for the playback bake (issue #1087).
 *
 * Unrolls each clip's loop iterations onto the timeline and inserts hold
 * points so the baked TE curve reproduces the model semantics exactly:
 * gaps hold the nearest clip edge, loop wraps jump, truncated final
 * iterations cut off at the clip end.
 *
 * Breakpoint VALUES always come from valueAtBeat (the lane-level resolver,
 * which owns overlap precedence and loop wrapping); the source points only
 * contribute positions and curve metadata (type / tension / handles) so the
 * bake loop tessellates curved segments the same way it does for absolute
 * lanes. Pure: no singletons, callbacks supply all state.
 *
 * @param lane        The clip-based lane (absolute lanes return empty).
 * @param getClip     Resolves a clip id to its info (may return null).
 * @param valueAtBeat The lane's value at a timeline beat.
 */
std::vector<AutomationPoint> flattenClipLane(
    const AutomationLaneInfo& lane,
    const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
    const std::function<double(double)>& valueAtBeat);

}  // namespace magda
