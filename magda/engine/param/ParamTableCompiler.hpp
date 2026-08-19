#pragma once

#include <vector>

#include "param/ParamTable.hpp"
#include "plan/RenderPlan.hpp"

namespace magda {
struct TrackInfo;
}

/**
 * @file ParamTableCompiler.hpp
 * @brief The model's parameters, resolved into the table a plan reads.
 *
 * Runs off the audio thread beside resolvePlanValues, over the same model, and
 * the division between the two is what each answers. The value table answers
 * what a mixer move did to an op. This answers what a device's own parameters
 * are: their scales, their stored values, and the macros and modifiers wired to
 * them.
 *
 * Walks the model rather than the plan, and takes the plan for its fingerprint
 * and its devices. A parameter belonging to something the plan does not carry
 * is still resolved and simply read by nobody, which is cheaper than working out
 * whether it is reachable and safer than guessing wrong.
 */

namespace magda::engine {

/**
 * @brief Compile every parameter of @p tracks and @p master against @p plan.
 *
 * @param plan    the plan the table is fingerprinted against
 * @param tracks  every non-master track, as passed to compileRenderPlan
 * @param master  the master track
 *
 * Diagnostics ride on the table (ParamTable::diagnostics): a link naming
 * something that does not exist, a target this table does not carry, a link to
 * a parameter that takes no modulation, and a cycle. Nothing is dropped in
 * silence, and nothing is refused: a table with diagnostics still renders,
 * minus whatever the diagnostic describes.
 */
ParamTable compileParamTable(const RenderPlan& plan, const std::vector<magda::TrackInfo>& tracks,
                             const magda::TrackInfo& master);

}  // namespace magda::engine
