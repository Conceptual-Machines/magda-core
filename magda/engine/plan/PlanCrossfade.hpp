#pragma once

#include <vector>

#include "plan/RenderPlan.hpp"

/**
 * @file PlanCrossfade.hpp
 * @brief Fading the edges an edit moved, inside the plan that replaces them.
 *
 * Carrying state across a swap (see PlanDiff.hpp) removes the clicks that come
 * from starting again: a delay line keeps its contents, a plugin keeps its tail.
 * It does nothing about the ones that come from the signal itself changing.
 * Insert a device mid-chain and block N+1 is a different signal from block N, and
 * no amount of carried state smooths that step. This is where that is answered.
 *
 * The obvious way to answer it does not work here. Rendering the old plan and
 * the new one side by side for the length of a fade would process every shared
 * plugin instance twice in one block, and the second call would see the wrong
 * input: instances are owned by the runtime store and deliberately shared across
 * epochs, which is what keeps a plugin alive through a recompile in the first
 * place. Two plans that share instances cannot both render.
 *
 * So the fade lives inside one plan rather than between two. The differ says
 * which ops changed; this pass runs over the compiler's output and puts a
 * Crossfade op on the audio edges the edit moved, reading the edge as it was and
 * the edge as it is, both of which the one plan already computes exactly once.
 * A fade is retired by the next publish rather than by anything on the audio
 * thread: an edge that has arrived where its fade was heading either re-emits it
 * (the fade is still running, and resumes) or drops it (it is spent, and the
 * plan goes back to being byte-for-byte what the compiler produced).
 *
 * The compiler is not involved and stays a pure function of the model. Nothing
 * here is a compiler mode, and no plan is ever mutated after it is published.
 */

namespace magda::engine {

/** A plan with fades on it, and what the pass did to get there. */
struct CrossfadedPlan {
    RenderPlan plan;

    /// Crossfade ops in @c plan. Includes fades re-emitted so a running one can
    /// carry on, because those are ops in the plan like any other.
    int inserted = 0;

    /// Audio edges the edit moved that this refused to fade. They step, as they
    /// do today; counted rather than passed over, because the difference between
    /// "nothing changed" and "something changed and it was not smoothed" is the
    /// only thing that says whether the pass is doing its job.
    int unfaded = 0;
};

/**
 * @brief Put fades on the audio edges @p newPlan moved, relative to @p oldPlan.
 *
 * Off the audio thread, between compiling a plan and resolving values for it:
 * the values are refused against any other plan, so they have to be resolved
 * against what comes out of here rather than what went in.
 *
 * An edge is faded only when every one of these holds, and stepped otherwise:
 *
 * - its consumer is an op the old plan had too, of the same kind and arity, that
 *   the differ did not carry, and the slot's producer names a different op
 * - the producer it used to read still exists in the new plan, earlier than the
 *   consumer, with that port still carrying audio
 * - that producer is computing what it used to: carried by the differ, and so is
 *   everything upstream of it. An old side fed by something that changed is not
 *   the old signal, it is a second changed signal
 * - neither end is a Delay. A delay's sample count is resolved from the fan-in
 *   it feeds, so interposing anything between the two resolves it against the
 *   wrong op, and a fade into one would be a fade into an edge that is about to
 *   be realigned anyway
 * - fading it would not make a deterministic op read a live one, which is
 *   provenance validatePlan enforces and this must not break
 *
 * @p stillFading is PlanExecutor::unfinishedCrossfades() from the epoch
 * rendering @p oldPlan, indexed by its OpIds. Empty means nothing is running,
 * which is what a caller with no epoch to ask should say.
 */
CrossfadedPlan insertCrossfades(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                                const std::vector<char>& stillFading = {});

}  // namespace magda::engine
