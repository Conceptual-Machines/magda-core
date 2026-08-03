#pragma once

#include <string>

#include "plan/RenderPlan.hpp"

namespace magda::engine {

/**
 * @brief Render a plan as canonical text.
 *
 * The dump is the plan's testable surface: golden tests assert it, and it is
 * what to look at when a compiled plan is not what a model change should have
 * produced. Deterministic and diff-friendly — one line per op, fixed columns,
 * no addresses or timings.
 *
 * Shape:
 * @code
 * magda-render-plan v1
 * ops=3 outputs=1
 * [  0] ClipAudio    det   T1:clipAudio        in=-      out=audio     deps=0
 * ...
 * ready=0
 * @endcode
 *
 * An input slot reads `<op>:<port>`, or `-` when the slot is unconnected.
 */
std::string dumpPlan(const RenderPlan& plan);

}  // namespace magda::engine
