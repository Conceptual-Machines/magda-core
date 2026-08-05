#pragma once

#include <vector>

#include "plan/RenderPlan.hpp"

/**
 * @file PlanDiff.hpp
 * @brief What survives when one plan replaces another.
 *
 * A structural edit compiles a new plan and swaps it in whole, and the naive
 * reading of that is that everything starts again: every delay line empty,
 * every reader rewound, every tail cut. That reading is the rebuild click, and
 * this is where it is answered.
 *
 * The expensive things are already safe. Plugin instances and file readers are
 * owned by the runtime store and keyed on model IDs, so they never noticed the
 * plan changed; that is deliberate and this must not undo it, because keying
 * them on plan membership is what tears a plugin down when a chain's power is
 * toggled. What is left is the state the executor owns per op, which today is
 * the delay lines and tomorrow is whatever slices 6 to 8 hang here.
 *
 * The join is on OpKey, which is the same identity the compiler assigns and
 * validatePlan proves unique. Matching keys is not enough to carry state
 * though: an op is only the same op if it also computes the same thing from
 * the same places, so the shape and the immediate inputs have to agree as well.
 * Whether the two are the same in the ways only a prepared plan can answer, a
 * delay's sample count among them, is checked where that is known.
 */

namespace magda::engine {

/**
 * @brief The carry decision for every op of a new plan.
 */
struct PlanDiff {
    /// Per op of the new plan: the op of the old plan whose runtime state it
    /// may adopt, or INVALID_OP_ID where there is none to adopt.
    std::vector<OpId> carriedFrom;

    /// Ops of the old plan nothing in the new one adopts. Their state goes
    /// when the epoch holding it does.
    std::vector<OpId> retired;

    /// How many ops carried, for tests and diagnostics.
    int carried = 0;
};

/**
 * @brief Match @p newPlan against @p oldPlan, op by op.
 *
 * An op carries when all of these hold:
 *
 * - the same OpKey, which is the model location and role the compiler gave it
 * - the same kind, and the same output ports in the same order
 * - the same input arity, with every slot connected in both or neither
 * - every connected slot fed by an op with the same key, through the same port
 *
 * The last one is what makes the decision local rather than recursive. If what
 * feeds an op has been replaced, whatever the op accumulated came from a signal
 * that is no longer arriving, and a delay line handing that back is a few
 * milliseconds of the wrong track. It also means an edit costs continuity only
 * where it lands and one op downstream of it, not along the whole chain.
 */
PlanDiff diffPlans(const RenderPlan& oldPlan, const RenderPlan& newPlan);

}  // namespace magda::engine
