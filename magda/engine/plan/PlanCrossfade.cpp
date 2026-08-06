#include "plan/PlanCrossfade.hpp"

#include <map>
#include <vector>

#include "plan/PlanDiff.hpp"

namespace magda::engine {
namespace {

/// How far a chain of fades is followed back. A plan holds one fade per edge, so
/// reaching this would mean an edit landing mid-fade on the same edge that many
/// times before any of them expired, which is not a shape the pass produces. It
/// is here so a hand-built plan cannot spin.
constexpr int kMaxFadeHops = 8;

/**
 * @brief The port a slot reads, seen through any fade already sitting on it.
 *
 * The plan being replaced may itself be one this pass produced. What a second
 * edit has to fade from is not that fade's own output, which is a mixture on its
 * way somewhere: it is the signal the first fade was going to, because that is
 * what the edge carries by the time anyone hears the second one.
 */
PortRef throughFades(const RenderPlan& plan, PortRef ref) {
    for (int hops = 0; hops < kMaxFadeHops && ref.valid(); ++hops) {
        const auto& op = plan.ops[static_cast<std::size_t>(ref.op)];
        if (op.kind != OpKind::Crossfade)
            return ref;
        ref = op.inputs[1];
    }
    return ref;
}

bool isAudioPort(const RenderPlan& plan, const PortRef& ref) {
    if (!ref.valid())
        return false;
    const auto& producer = plan.ops[static_cast<std::size_t>(ref.op)];
    return ref.port >= 0 && ref.port < static_cast<int>(producer.outputs.size()) &&
           producer.outputs[static_cast<std::size_t>(ref.port)] == SignalKind::Audio;
}

LivenessDomain livenessOf(const RenderPlan& plan, const PortRef& ref) {
    return plan.ops[static_cast<std::size_t>(ref.op)].liveness;
}

/// One fade to insert: the op that reads the edge, the slot it reads it on, and
/// the port in the new plan carrying what that slot used to get.
struct PendingFade {
    OpId consumer = INVALID_OP_ID;
    int slot = 0;
    PortRef oldSide;
};

/// What one slot of one op turned out to need.
enum class SlotVerdict {
    Unmoved,     ///< reads the same place it did; nothing to do
    Fade,        ///< moved, and the signal it moved off is still available
    CannotFade,  ///< moved, and it is not
};

struct SlotDecision {
    SlotVerdict verdict = SlotVerdict::Unmoved;
    /// Where in the new plan the old signal is, when the verdict is Fade.
    PortRef oldSide;
};

/**
 * @brief Works out where the fades go, over one pair of plans.
 *
 * A class only because every decision reads the same five things, and threading
 * them through free functions would be five parameters everywhere.
 */
class FadeFinder {
  public:
    FadeFinder(const RenderPlan& oldPlan, const RenderPlan& newPlan,
               const std::vector<char>& stillFading)
        : old_(oldPlan),
          new_(newPlan),
          stillFading_(stillFading),
          diff_(diffPlans(oldPlan, newPlan)) {
        for (std::size_t i = 0; i < old_.ops.size(); ++i)
            oldByKey_.emplace(old_.ops[i].key, static_cast<OpId>(i));
        for (std::size_t i = 0; i < new_.ops.size(); ++i)
            newByKey_.emplace(new_.ops[i].key, static_cast<OpId>(i));

        // Carrying is local, and the old side of a fade has to be more than
        // that: it has to be the signal that was actually there, which means the
        // whole subtree behind it computing what it used to. One forward pass,
        // because ops are in dependency order.
        unchanged_.assign(new_.ops.size(), 0);
        for (std::size_t i = 0; i < new_.ops.size(); ++i) {
            if (diff_.carriedFrom[i] == INVALID_OP_ID)
                continue;
            auto intact = true;
            for (const auto& input : new_.ops[i].inputs)
                if (input.valid() && unchanged_[static_cast<std::size_t>(input.op)] == 0)
                    intact = false;
            unchanged_[i] = intact ? 1 : 0;
        }
    }

    std::vector<PendingFade> find() {
        std::vector<PendingFade> fades;

        for (std::size_t i = 0; i < new_.ops.size(); ++i) {
            if (diff_.carriedFrom[i] != INVALID_OP_ID)
                continue;

            const auto& after = new_.ops[i];

            // A fade needs an op that was there before and reads somewhere else
            // now. An op the edit created has no old signal of its own: whatever
            // it displaced is faded where that lands, one op downstream.
            const auto wasThere = oldByKey_.find(after.key);
            if (wasThere == oldByKey_.end())
                continue;

            const auto& before = old_.ops[static_cast<std::size_t>(wasThere->second)];
            if (before.kind != after.kind)
                continue;

            if (before.inputs.size() != after.inputs.size()) {
                // A sum that gained or lost an input. Nothing here maps one slot
                // to another, so there is no edge to fade, but the signal did
                // change and saying so is the point of the counter.
                for (const auto& input : after.inputs)
                    if (isAudioPort(new_, input)) {
                        ++unfaded;
                        break;
                    }
                continue;
            }

            for (std::size_t slot = 0; slot < after.inputs.size(); ++slot) {
                const auto decision = decide(static_cast<OpId>(i), before, slot);
                if (decision.verdict == SlotVerdict::CannotFade)
                    ++unfaded;
                if (decision.verdict == SlotVerdict::Fade)
                    fades.push_back(
                        {static_cast<OpId>(i), static_cast<int>(slot), decision.oldSide});
            }
        }

        return fades;
    }

    int unfaded = 0;

  private:
    /// Whether the op behind @p ref is a fade of @p old_ that has not finished.
    bool isRunningFade(const PortRef& ref) const {
        return ref.valid() &&
               old_.ops[static_cast<std::size_t>(ref.op)].kind == OpKind::Crossfade &&
               static_cast<std::size_t>(ref.op) < stillFading_.size() &&
               stillFading_[static_cast<std::size_t>(ref.op)] != 0;
    }

    SlotDecision decide(OpId consumer, const PlanOp& before, std::size_t slot) const {
        const auto& after = new_.ops[static_cast<std::size_t>(consumer)];
        const auto& to = after.inputs[slot];
        if (!isAudioPort(new_, to))
            return {};

        // Where the old edge was heading, which is what it carries by the time
        // anyone hears this edit: a fade already on it is on its way to its new
        // side, not sitting on its old one.
        const auto heading = throughFades(old_, before.inputs[slot]);
        if (!isAudioPort(old_, heading))
            return {};

        const auto arrived = new_.ops[static_cast<std::size_t>(to.op)].key ==
                                 old_.ops[static_cast<std::size_t>(heading.op)].key &&
                             to.port == heading.port;

        // A fade on this slot that has not finished is re-emitted on the same key
        // so the ramp carries: dropping it would cut it where it had got to,
        // which is the step it was there to smooth. One that has finished is left
        // out, and the plan is back to what the compiler produced.
        const auto running = isRunningFade(before.inputs[slot]);
        if (arrived && !running)
            return {};

        const auto from =
            arrived
                ? throughFades(old_,
                               old_.ops[static_cast<std::size_t>(before.inputs[slot].op)].inputs[0])
                : heading;
        if (!isAudioPort(old_, from))
            return {};

        const auto stillHere = newByKey_.find(old_.ops[static_cast<std::size_t>(from.op)].key);
        if (stillHere == newByKey_.end())
            return {SlotVerdict::CannotFade, {}};  // removed: nothing computes it any more

        const PortRef oldSide{stillHere->second, from.port};

        // Ops are in dependency order, so a producer that now sits after the op
        // reading it is a reorder, and reading it here would be a cycle. Nothing
        // else in the pass has to reason about cycles.
        if (oldSide.op >= consumer || !isAudioPort(new_, oldSide))
            return {SlotVerdict::CannotFade, {}};

        // Same key, different signal: the op is still there and still feeds this
        // slot, but something behind it moved, so what it puts out is not what
        // this edge used to carry.
        if (unchanged_[static_cast<std::size_t>(oldSide.op)] == 0)
            return {SlotVerdict::CannotFade, {}};

        // Never between a delay and the op it compensates. A delay's sample count
        // is resolved from the fan-in it feeds, so moving its consumer to a fade
        // would silently recompute it against the wrong thing. The edit shows up
        // on the delay's own input instead, which is where the fade lands and
        // where it belongs.
        if (new_.ops[static_cast<std::size_t>(to.op)].kind == OpKind::Delay)
            return {SlotVerdict::CannotFade, {}};

        // The new side is one of this op's own inputs, so it can only be live
        // where this op is. The old side is under no such obligation: a track
        // switching from its live input to its clips leaves a deterministic op
        // reading a live signal, which is exactly the provenance rule
        // validatePlan exists to keep.
        if (livenessOf(old_, from) == LivenessDomain::Live &&
            after.liveness == LivenessDomain::Deterministic)
            return {SlotVerdict::CannotFade, {}};

        // No room in the key for a slot this far out. Unreachable for any plan
        // the compiler produces, and silently wrong if it ever were not.
        if (slot >= static_cast<std::size_t>(kCrossfadeRoleStride))
            return {SlotVerdict::CannotFade, {}};

        return {SlotVerdict::Fade, oldSide};
    }

    const RenderPlan& old_;
    const RenderPlan& new_;
    const std::vector<char>& stillFading_;
    PlanDiff diff_;
    std::map<OpKey, OpId> oldByKey_, newByKey_;
    std::vector<char> unchanged_;
};

/**
 * @brief @p plan with @p fades inserted ahead of the ops that read them.
 *
 * Rebuilt rather than patched, because a fade goes in the middle and dependency
 * order is what makes every walk over a plan a straight one. @p fades must be in
 * consumer order, which is how the finder collects them.
 */
RenderPlan withFades(const RenderPlan& plan, const std::vector<PendingFade>& fades) {
    RenderPlan built;
    built.version = plan.version;
    built.diagnostics = plan.diagnostics;
    built.ops.reserve(plan.ops.size() + fades.size());

    std::vector<OpId> moved(plan.ops.size(), INVALID_OP_ID);
    std::size_t nextFade = 0;

    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        auto op = plan.ops[i];

        // Inputs first, because the fades below read them as they now are.
        for (auto& input : op.inputs)
            if (input.valid())
                input.op = moved[static_cast<std::size_t>(input.op)];

        for (; nextFade < fades.size() && fades[nextFade].consumer == static_cast<OpId>(i);
             ++nextFade) {
            const auto& fade = fades[nextFade];
            const PortRef oldSide{moved[static_cast<std::size_t>(fade.oldSide.op)],
                                  fade.oldSide.port};
            const auto& newSide = op.inputs[static_cast<std::size_t>(fade.slot)];

            PlanOp faded;
            faded.kind = OpKind::Crossfade;
            faded.key = op.key;
            faded.key.role = OpRole::EdgeCrossfade;
            faded.key.index = crossfadeIndex(op.key.role, fade.slot);
            faded.liveness = livenessOf(built, oldSide) == LivenessDomain::Live ||
                                     livenessOf(built, newSide) == LivenessDomain::Live
                                 ? LivenessDomain::Live
                                 : LivenessDomain::Deterministic;
            faded.inputs = {oldSide, newSide};
            faded.outputs = {SignalKind::Audio};

            const auto fadeOp = static_cast<OpId>(built.ops.size());
            built.ops.push_back(std::move(faded));
            op.inputs[static_cast<std::size_t>(fade.slot)] = PortRef{fadeOp, 0};
        }

        moved[i] = static_cast<OpId>(built.ops.size());
        built.ops.push_back(std::move(op));
    }

    built.outputOps.reserve(plan.outputOps.size());
    for (const auto outputOp : plan.outputOps)
        built.outputOps.push_back(moved[static_cast<std::size_t>(outputOp)]);

    bakeScheduling(built);
    return built;
}

}  // namespace

CrossfadedPlan insertCrossfades(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                                const std::vector<char>& stillFading) {
    CrossfadedPlan result;
    result.plan = newPlan;

    if (oldPlan.ops.empty() || newPlan.ops.empty())
        return result;

    FadeFinder finder(oldPlan, newPlan, stillFading);
    const auto fades = finder.find();
    result.unfaded = finder.unfaded;

    if (fades.empty())
        return result;

    result.inserted = static_cast<int>(fades.size());
    result.plan = withFades(newPlan, fades);
    return result;
}

}  // namespace magda::engine
