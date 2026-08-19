#pragma once

#include <functional>
#include <span>

#include "AutomationInfo.hpp"

/**
 * @file AutomationCurve.hpp
 * @brief What a breakpoint list is worth at a beat.
 *
 * The shape of a lane, on its own: no manager, no singleton, no lane and no
 * clip, so the audio engine can bake a curve (#2118) through the same function
 * the editor draws it with and AutomationManager plays it with. Two readings of
 * one curve is a project that sounds different from the way it looks.
 *
 * Pure and allocation-free.
 */

namespace magda::automation {

/**
 * @brief The value of @p points at @p beat.
 *
 * Points are in beat order. Before the first and after the last the curve
 * holds, which is what makes a lane cover the whole timeline once it has a
 * point on it at all. An empty list has no value to give and answers the middle
 * of the range, as every other reading of an empty curve in MAGDA does.
 *
 * Each segment is shaped by the curve type of the point that opens it: a linear
 * one through the shared bend (curvemath::evalSegment), a bezier through the
 * parametric cubic solved for the queried beat, a step held, and a hard corner
 * as two straight runs meeting at its apex.
 */
double valueAtBeat(std::span<const AutomationPoint> points, double beat);

/**
 * @brief The value of a whole lane at a timeline beat.
 *
 * An absolute lane is its points. A clip-based one is whichever clip contains
 * the beat, read at its local position so a looping clip repeats; between clips
 * the lane holds the nearest edge, which is what makes a gap a plateau rather
 * than a jump to nowhere.
 *
 * @p getClip resolves a clip id, because a lane names its clips and does not
 * own them. Returning null for one is a clip the lane lists and the project has
 * lost, and it is skipped rather than guessed at.
 */
double laneValueAtBeat(const AutomationLaneInfo& lane,
                       const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
                       double beat);

}  // namespace magda::automation
