#pragma once

#include <cstdint>
#include <vector>

#include "param/ParamTable.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file ModulationLiveness.hpp
 * @brief Liveness carried through modulation, which is not a plan edge (#1898).
 *
 * A device modulated by an envelope a live keyboard triggers depends on the outside world as
 * much as one reading the keyboard does, and a modifier advanced on two threads at two blocks is
 * two modifiers. So everything a modulation link joins (parameters, macros, modifiers and the
 * ops reading them) sits on one side: when any of it is live, or read by an op that runs at the
 * callback, every op reading it and every tap feeding its modifiers is promoted to live, and
 * liveness flows on downstream of them.
 */

namespace magda::engine {

/// The table parameters @p op reads, as the executor binds them.
std::vector<ParamId> paramsReadBy(const PlanOp& op, const ParamTable& table);

/**
 * @brief Promote what modulation makes live in @p plan; true when anything changed.
 *
 * Off the audio thread, after compiling and before handoffs. Promoted ops carry
 * PlanOp::liveByModulation, which is what lets validatePlan accept a live op with nothing live
 * behind it. Feedback carries are settled with the rest.
 */
bool promoteModulatedLiveness(RenderPlan& plan, const ParamTable& table);

/// Which side resolves each parameter and advances each modifier: 1 for the callback, 0 for
/// what may be rendered ahead. A component is wholly one side once promotion has settled it.
struct ModulationSides {
    std::vector<std::uint8_t> params;
    std::vector<std::uint8_t> modifiers;
    /// Per op, the side whose values it reads: its component's, or for an op reading no
    /// parameters whether it runs at the callback. The two agree on a promoted plan.
    std::vector<std::uint8_t> ops;
};
ModulationSides modulationSides(const RenderPlan& plan, const ParamTable& table);

/// Fill @p table's side fields against @p plan. Off the audio thread, with the table.
void assignSides(const RenderPlan& plan, ParamTable& table);

}  // namespace magda::engine
