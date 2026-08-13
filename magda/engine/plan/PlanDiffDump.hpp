#pragma once

#include <string>

#include "plan/PlanDiff.hpp"
#include "plan/RenderPlan.hpp"

namespace magda::engine {

/**
 * @brief Render a diff between two plans as canonical text.
 *
 * What the differ decided, by op key rather than by op index: an op that
 * carried says which key it carried from, and a retired op says which key went
 * away. Indices move whenever anything is inserted ahead of an op, so a golden
 * written in terms of them would churn on edits that carried everything
 * correctly.
 *
 * Shape:
 * @code
 * magda-plan-diff v1
 * ops=13 carried=12 retired=1
 * [  0] carry   T1:clipAudio                   from=T1:clipAudio
 * [  2] new     T1/D9:deviceProcess
 * retired T1/D7:deviceProcess
 * @endcode
 */
std::string dumpPlanDiff(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                         const PlanDiff& diff);

}  // namespace magda::engine
