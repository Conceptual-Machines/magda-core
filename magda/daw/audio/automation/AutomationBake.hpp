#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>

#include "../../core/AutomationInfo.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Write one MAGDA lane into one Tracktion curve.
 *
 * MAGDA owns the curve model and Tracktion's iterator is linear only, so a
 * lane is not copied across: it is emitted as the breakpoints a linear
 * iterator has to be given for the two to describe the same shape. A step
 * segment gets a point just before its edge so the old value holds right up to
 * the jump; a bezier, a tensioned linear or a hard corner is tessellated,
 * because two endpoints would be baked as a straight ramp whatever the user
 * drew.
 *
 * Pure, and every input a callback, for the reason `flattenClipLane` is: this
 * runs from the playback engine over the automation singleton, and from the
 * null-diff corpus over a case's own lanes (#2123). One implementation is what
 * makes those two comparable at all. A bake written a second time for the
 * harness would be a harness that agrees with itself.
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

}  // namespace magda
