#pragma once

#include <string>
#include <vector>

#include "plan/RenderPlan.hpp"

/**
 * @file PlanHandoff.hpp
 * @brief The boundary between what may be rendered ahead and what runs at the callback (#1898).
 *
 * A deterministic op may be rendered ahead of the transport; a live op, and an op handing a
 * signal to the hardware, runs when the callback asks for its block. Wherever the second reads
 * the first, a Handoff stands on the edge, so an anticipative executor has one kind of op to put
 * its read-ahead behind. Until it does, a handoff passes its input through and the plan renders
 * exactly what it rendered without one.
 */

namespace magda::engine {

/// Whether @p op runs at the callback whatever is rendered ahead: it is live, or it hands audio
/// or MIDI to the hardware.
bool runsAtCallback(const PlanOp& op);

/**
 * @brief @p plan with a Handoff on every port a callback op reads from a deterministic one.
 *
 * One handoff per port, placed before its first reader and shared by the rest. Off the audio
 * thread, after any crossfades and before values are resolved, which are refused against any
 * other plan. A plan that already carries its handoffs comes back unchanged.
 */
RenderPlan insertHandoffs(const RenderPlan& plan);

/// One message per callback op reading a deterministic op other than a handoff.
std::vector<std::string> handoffProblems(const RenderPlan& plan);

}  // namespace magda::engine
