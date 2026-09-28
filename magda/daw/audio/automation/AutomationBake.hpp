#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>

#include "../../core/AutomationInfo.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Write one MAGDA lane into one Tracktion curve.
 *
 * Tracktion's iterator is linear only, so a lane is emitted as the breakpoints
 * that iterator needs to describe the same shape: a step gets a point just
 * before its edge, and a bezier, tensioned linear or hard corner is tessellated
 * because two endpoints would play as a straight ramp whatever the user drew.
 *
 * Pure, every input a callback, for the reason `flattenClipLane` is: this runs
 * from the playback engine over the automation singleton and from the null-diff
 * corpus over a case's own lanes (#2123). A second bake written for the harness
 * would be a harness that agrees with itself.
 *
 * @param curve             the Tracktion curve to fill; cleared by the caller
 * @param lane              the lane to emit
 * @param getClip           resolves a clip id for a clip-based lane
 * @param valueAtBeat       the lane's value at a timeline beat
 * @param toParameterValue  MAGDA's normalised 0..1 to what the parameter stores
 */
void bakeLaneIntoCurve(te::AutomationCurve& curve, const AutomationLaneInfo& lane,
                       const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
                       const std::function<double(double)>& valueAtBeat,
                       const std::function<float(double)>& toParameterValue);

/**
 * @brief MAGDA's normalised 0..1 to what @p param stores, for @p target.
 *
 * The `toParameterValue` above, built once for a lane. Here beside the bake for
 * the same reason the bake is here: the playback engine and the null-diff
 * corpus both write a lane into a Tracktion curve, and a second conversion
 * written for the corpus would be a corpus that agrees with itself about what a
 * lane means. A device parameter's mapping is not a formula anybody would
 * reproduce by reading the other one -- an internal plugin on a 0..1 native
 * range with a display scale over it, an external one whose detected display
 * range is not its native one, and a macro that is 0..1 on both sides all take
 * a different branch.
 *
 * Built rather than called per point on purpose. Resolving the target's
 * ParameterInfo walks the track and rack tree and copies choices, value tables
 * and shared pointers; doing that inside the loop is enough to stall play and
 * stop on any edit with automation on a plugin parameter.
 *
 * @param target  what the lane plays over
 * @param param   the Tracktion parameter it resolves to, or null where the
 *                target has none; used only for its own range, as a last
 *                resort for a parameter whose info carries none
 */
std::function<float(double)> makeParameterValueConverter(const AutomationTarget& target,
                                                         te::AutomatableParameter* param);

}  // namespace magda
