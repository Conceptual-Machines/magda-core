#pragma once

#include <vector>

#include "plan/RenderPlan.hpp"

/**
 * @file PlanCrossfade.hpp
 * @brief Fading the edges a structural edit changed, inside the new plan.
 *
 * Carrying runtime state across a swap removes the clicks that come from
 * starting again: a delay line that keeps its contents, a plugin that keeps its
 * tail. It does not remove the ones that come from the signal itself changing.
 * Inserting a device makes block N+1 a different signal from block N, and
 * nothing about carrying state smooths that step.
 *
 * The obvious way to smooth it does not work here. Rendering the old and new
 * plans side by side for the length of a fade would process every shared plugin
 * instance twice in one block, with the second call seeing the wrong input:
 * instances are owned by the runtime store and keyed on model IDs, deliberately
 * shared across epochs, which is what keeps a plugin alive through a recompile
 * in the first place. Moving the fade inside one plan does not help by itself,
 * because a shadow subgraph naming a device the main path also names binds to
 * that same instance. The constraint is not that two plans cannot both render.
 * It is that any op carrying a store-owned instance may appear once.
 *
 * That constraint is what decides where the fade goes. Insert a device D
 * between A and B, and the new plan already holds everything a fade needs: A
 * runs once, D runs once, and a fade op reading A's output and D's output feeds
 * B. B runs once, on the faded signal. Nothing is duplicated, because the old
 * signal at that point is A's output, which is in the plan regardless. Reads of
 * a shared port are already safe in both executors.
 *
 * So this is a pass over a compiled plan rather than a mode of the compiler.
 * The compiler stays a pure function of the model, which is what makes plans
 * golden-testable, and the fade ops are inserted afterwards from what the differ
 * says changed.
 *
 * Call order matters, because values are resolved against a plan and refused
 * against any other:
 *
 * @code
 * auto plan = compileRenderPlan(tracks, master);
 * auto faded = insertCrossfades(*livePlan, plan, liveExecutor.unfinishedCrossfades());
 * resolvePlanValues(faded.plan, tracks, master, values);
 * session.publish(std::make_shared<const RenderPlan>(std::move(faded.plan)), ...);
 * @endcode
 */

namespace magda::engine {

/**
 * @brief A new plan with fades on the edges that changed.
 */
struct CrossfadedPlan {
    /// The plan to publish. The new plan unchanged when there was nothing to
    /// fade, which is the common case: most edits change no audio edge at all.
    RenderPlan plan;

    /// Fade ops inserted.
    int inserted = 0;

    /// Audio edges that changed and could not be faded. Not a failure and not
    /// reported as a diagnostic: it is the ordinary answer for a reorder, and
    /// counting it is how a test tells "nothing changed" from "nothing could be
    /// done about it".
    int unfaded = 0;
};

/**
 * @brief Insert fades into @p newPlan for the edges @p oldPlan carried
 *        differently.
 *
 * An edge is faded when the signal it used to carry is still in the new plan
 * and still upstream of the op that reads it. Both halves matter:
 *
 * - Still in the plan rules out removal. Take a device out of a chain and the
 *   old signal is that device's output, which nothing computes any more.
 *   Reconstructing it means putting the device back for the length of the fade,
 *   which is a bigger change than this one.
 * - Still upstream rules out reordering. Swap two devices and each one appears
 *   exactly once, but the old ordering needs both of them twice, and the op that
 *   used to be first now sits after the one that reads it. Ops are in dependency
 *   order, so that shows up as an index comparison and needs no cycle check.
 *
 * And the op carrying it has to be computing what it used to, transitively. The
 * differ's own answer to that is deliberately local: it says an op takes the
 * same inputs from the same places, which is what deciding whether to carry a
 * delay line needs. It is not what a fade needs. Reorder two devices and the
 * second one still has its key and still feeds the fader, but it is processing
 * the dry signal now, so reading it as the old side would fade from something
 * nobody heard.
 *
 * Where none of that holds the edge steps as it does today, and @ref
 * CrossfadedPlan::unfaded counts it. Fading in from silence instead would be a
 * dip, and repeated at the speed someone drags a device around, a dip is worse
 * than the step it replaces.
 *
 * An op whose input count changed is left alone too. A sum gaining a send is not
 * an edge moving, it is an edge appearing, and the signal it used to carry is
 * the sum without it: expressible, but as a second sum rather than as a fade on
 * an edge, which is a bigger change than this one.
 *
 * MIDI edges are left alone: a merge changing what reaches it does not click,
 * and there is no such thing as half an event.
 *
 * @p oldPlan may itself be a plan this returned, and normally is.
 *
 * @p stillFading is the epoch rendering @p oldPlan reporting which of its fades
 * have not finished (PlanExecutor::unfinishedCrossfades). It is what makes a
 * fade retire without a plan of its own: one still running is re-emitted on the
 * same key so the ramp carries and finishes, and one that has finished is left
 * out, which is how the plan gets back to what the compiler produced. Empty
 * reads as "none of them are", which is right for a first publish and for any
 * caller that does not fade.
 */
CrossfadedPlan insertCrossfades(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                                const std::vector<char>& stillFading = {});

}  // namespace magda::engine
