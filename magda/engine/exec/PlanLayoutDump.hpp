#pragma once

#include <string>
#include <vector>

#include "exec/PlanLayout.hpp"
#include "plan/RenderPlan.hpp"

namespace magda::engine {

/**
 * @brief Render a prepared plan's latency and buffer assignment as canonical text.
 *
 * The counterpart to dumpPlan for the two decisions that are not in the plan:
 * how many samples each delay holds, and which arena slot each port renders
 * into. Both follow from binding a plan to the instances behind it, so both
 * are pinned against the device latencies they were resolved with rather than
 * on their own.
 *
 * Shape:
 * @code
 * magda-plan-layout v1
 * audioSlots=3 midiSlots=1 outputLatency=64
 * [  0] ClipAudio    T1:clipAudio        lat=0     ports=a0
 * [  1] Delay        T-2:mixInputDelay   lat=64    ports=a1        hold=64
 * ...
 * @endcode
 *
 * A port reads `a<slot>` or `m<slot>` for the arena it indexes. `inplace` and
 * `elided` are printed only where set, so a fixture that provokes neither
 * prints nothing about them.
 */
std::string dumpPlanLayout(const RenderPlan& plan, const std::vector<int>& deviceLatency);

}  // namespace magda::engine
