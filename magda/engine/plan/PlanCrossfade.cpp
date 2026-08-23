#include "plan/PlanCrossfade.hpp"

#include <algorithm>
#include <map>

#include "plan/PlanDiff.hpp"

namespace magda::engine {
namespace {

/// The signal a port carries, or Midi where there is no port to ask.
SignalKind kindOf(const RenderPlan& plan, const PortRef& ref) {
    if (!ref.valid())
        return SignalKind::Midi;
    const auto& outputs = plan.ops[static_cast<std::size_t>(ref.op)].outputs;
    const auto port = static_cast<std::size_t>(ref.port);
    return port < outputs.size() ? outputs[port].kind : SignalKind::Midi;
}

/// Whether two edges, one in each plan, name the same producer and port. Keys
/// rather than indices: an edit anywhere earlier moves every op after it.
bool sameEdge(const RenderPlan& oldPlan, const PortRef& before, const RenderPlan& newPlan,
              const PortRef& after) {
    if (before.valid() != after.valid())
        return false;
    if (!before.valid())
        return true;
    return before.port == after.port && oldPlan.ops[static_cast<std::size_t>(before.op)].key ==
                                            newPlan.ops[static_cast<std::size_t>(after.op)].key;
}

std::map<OpKey, OpId> keysOf(const RenderPlan& plan) {
    std::map<OpKey, OpId> byKey;
    for (std::size_t i = 0; i < plan.ops.size(); ++i)
        byKey.emplace(plan.ops[i].key, static_cast<OpId>(i));
    return byKey;
}

/// One fade to emit in front of one op.
struct Fade {
    /// The consumer input slot it fills. A consumer can be faded on more than
    /// one slot, and the last fade emitted for a slot is the one it reads.
    std::size_t slot = 0;
    OpKey key;

    /// Where the old side comes from: an op of the new plan, or an earlier fade
    /// in the same list, which is what a fade stacked on a running one reads.
    PortRef oldSide;
    int oldSideFade = -1;

    /// Always an op of the new plan: a fade is always heading somewhere real.
    PortRef newSide;
};

class Pass {
  public:
    Pass(const RenderPlan& oldPlan, const RenderPlan& newPlan, const std::vector<char>& stillFading)
        : old_(oldPlan),
          now_(newPlan),
          stillFading_(stillFading),
          diff_(diffPlans(oldPlan, newPlan)),
          oldByKey_(keysOf(oldPlan)),
          newByKey_(keysOf(newPlan)),
          fades_(newPlan.ops.size()),
          unchanged_(newPlan.ops.size(), 0) {}

    CrossfadedPlan run();

  private:
    /// Whether an op of the old plan is a fade that has not finished. Indexed
    /// straight, because stillFading_ came from the epoch rendering that plan.
    bool isRunning(OpId op) const {
        const auto index = static_cast<std::size_t>(op);
        return old_.ops[index].kind == OpKind::Crossfade && index < stillFading_.size() &&
               stillFading_[index] != 0;
    }

    /// The fades an edge of the old plan goes through, outermost first, and
    /// whether the walk reached the end of them. A chain longer than a key can
    /// hold cannot be reproduced, and a plan should never contain one.
    std::vector<OpId> chainOf(PortRef ref, bool& whole) const;

    /// Decide one op's slots. Returns whether the op is computing what it
    /// computed before, which is the seed the forward pass needs.
    bool visitConsumer(std::size_t consumer, const PlanOp& before, CrossfadedPlan& result);
    bool visitSlot(std::size_t consumer, const PlanOp& before, std::size_t slot,
                   CrossfadedPlan& result);

    /// Whether a fade may be put on this slot at all, whatever it would fade
    /// between: the shape of the edge rather than the signals on it.
    bool canFade(std::size_t consumer, std::size_t slot, int depth) const;

    /// Where an edge of the old plan is in the new one. @p mustBeUnchanged for
    /// an old side being chosen, which has to be the signal it used to be;
    /// false for a fade being kept, which reads the edges it already read
    /// whatever they now carry.
    bool resolveSide(std::size_t consumer, const PortRef& oldSide, bool mustBeUnchanged,
                     PortRef& resolved) const;

    /// Re-emit a chain of running fades, innermost first, so what is audible
    /// now is what the new plan starts from. False when any of them cannot be
    /// reproduced, and @p emit is then not to be used. @p base is where @p emit
    /// will land in the consumer's list, because a fade reading another names
    /// it by its place there.
    bool retain(std::size_t consumer, std::size_t slot, const std::vector<OpId>& chain,
                std::size_t base, std::vector<Fade>& emit) const;

    /// Every input of an op that already counts as unchanged in itself.
    bool inputsUnchanged(std::size_t op) const {
        for (const auto& input : now_.ops[op].inputs)
            if (input.valid() && unchanged_[static_cast<std::size_t>(input.op)] == 0)
                return false;
        return true;
    }

    OpKey fadeKey(std::size_t consumer, std::size_t slot, int depth) const {
        auto key = now_.ops[consumer].key;
        key.role = OpRole::EdgeCrossfade;
        key.index = crossfadeIndex(now_.ops[consumer].key.role, now_.ops[consumer].key.index,
                                   static_cast<int>(slot), depth);
        return key;
    }

    RenderPlan build(int& inserted) const;

    const RenderPlan& old_;
    const RenderPlan& now_;
    const std::vector<char>& stillFading_;
    PlanDiff diff_;
    std::map<OpKey, OpId> oldByKey_, newByKey_;

    /// Per op of the new plan: the fades to emit in front of it, in the order
    /// they are emitted. Innermost first, so a stacked fade's old side is
    /// already there when it is built.
    std::vector<std::vector<Fade>> fades_;

    /// Per op of the new plan: it carried, and so did everything it reads. The
    /// only thing a new fade may take as its old side.
    std::vector<char> unchanged_;
};

std::vector<OpId> Pass::chainOf(PortRef ref, bool& whole) const {
    std::vector<OpId> chain;
    whole = true;

    while (ref.valid() && old_.ops[static_cast<std::size_t>(ref.op)].kind == OpKind::Crossfade) {
        if (chain.size() >= static_cast<std::size_t>(kCrossfadeMaxDepth)) {
            whole = false;
            break;
        }
        chain.push_back(ref.op);
        ref = old_.ops[static_cast<std::size_t>(ref.op)].inputs.front();
    }

    return chain;
}

bool Pass::canFade(std::size_t consumer, std::size_t slot, int depth) const {
    const auto& after = now_.ops[consumer];
    if (slot >= after.inputs.size() || !after.inputs[slot].valid())
        return false;

    // A delay's count is read off the fan-in it feeds. A fade in front of one
    // would be resolved against the fade instead, and a fade behind one would
    // put the delay where the fan-in cannot see it: neither is a fade, both are
    // a compensation bug that only shows up on a session with latency in it.
    if (after.kind == OpKind::Delay)
        return false;
    if (now_.ops[static_cast<std::size_t>(after.inputs[slot].op)].kind == OpKind::Delay)
        return false;

    // The key is four numbers in one integer, and a field that does not fit
    // would land in the next one's bits.
    return slot < static_cast<std::size_t>(kCrossfadeMaxSlot) && after.key.index >= 0 &&
           after.key.index < kCrossfadeMaxIndex && depth >= 0 && depth < kCrossfadeMaxDepth;
}

bool Pass::resolveSide(std::size_t consumer, const PortRef& oldSide, bool mustBeUnchanged,
                       PortRef& resolved) const {
    if (!oldSide.valid())
        return false;

    // Ops are in dependency order, so one integer comparison is also the whole
    // of the cycle check.
    const auto producer = newByKey_.find(old_.ops[static_cast<std::size_t>(oldSide.op)].key);
    if (producer == newByKey_.end() || producer->second >= static_cast<OpId>(consumer))
        return false;

    const auto& producerOp = now_.ops[static_cast<std::size_t>(producer->second)];
    if (producerOp.kind == OpKind::Delay || producerOp.kind == OpKind::Crossfade)
        return false;

    const auto port = static_cast<std::size_t>(oldSide.port);
    if (port >= producerOp.outputs.size() || producerOp.outputs[port].kind != SignalKind::Audio)
        return false;

    // What it is computing, not only that it is still there. A producer whose
    // own inputs moved hands back a signal that was never the old one.
    if (mustBeUnchanged && unchanged_[static_cast<std::size_t>(producer->second)] == 0)
        return false;

    // Liveness travels downstream and validatePlan checks it both ways, so a
    // fade cannot be the thing that puts a live signal in front of an op the
    // anticipative executor is allowed to run early.
    if (producerOp.liveness == LivenessDomain::Live &&
        now_.ops[consumer].liveness == LivenessDomain::Deterministic)
        return false;

    resolved = PortRef{producer->second, oldSide.port};
    return true;
}

bool Pass::retain(std::size_t consumer, std::size_t slot, const std::vector<OpId>& chain,
                  std::size_t base, std::vector<Fade>& emit) const {
    // Innermost first, so each one's old side is either a real op or the fade
    // emitted just before it.
    for (auto level = chain.size(); level-- > 0;) {
        const auto& fade = old_.ops[static_cast<std::size_t>(chain[level])];
        if (!canFade(consumer, slot, crossfadeDepth(fade.key.index)))
            return false;

        Fade kept;
        kept.slot = slot;
        kept.key = fade.key;

        // Kept as it is, not re-decided: it is already running, and what it
        // reads is what it has to go on reading for the sample after the swap
        // to follow the one before it. Whether those edges have changed
        // underneath is not this fade's business; anything that changed one of
        // them has its own fade smoothing it.
        if (!resolveSide(consumer, fade.inputs[1], false, kept.newSide))
            return false;

        if (level + 1 == chain.size()) {
            if (!resolveSide(consumer, fade.inputs[0], false, kept.oldSide))
                return false;
        } else {
            kept.oldSideFade = static_cast<int>(base + emit.size()) - 1;
        }

        emit.push_back(kept);
    }

    return true;
}

bool Pass::visitSlot(std::size_t consumer, const PlanOp& before, std::size_t slot,
                     CrossfadedPlan& result) {
    const auto beforeRef = before.inputs[slot];
    const auto afterRef = now_.ops[consumer].inputs[slot];

    bool whole = true;
    const auto chain = chainOf(beforeRef, whole);

    // What the edge is, or was heading for. A fade's destination is what it
    // becomes once it is spent, so an edge that has arrived there did not move.
    const auto destination =
        chain.empty() ? beforeRef : old_.ops[static_cast<std::size_t>(chain.front())].inputs[1];
    const auto running = !chain.empty() && isRunning(chain.front());
    const auto arrived = sameEdge(old_, destination, now_, afterRef);

    if (arrived && !running)
        return true;  // nothing moved, or a spent chain is retiring off the edge

    const auto audio = kindOf(now_, afterRef) == SignalKind::Audio ||
                       kindOf(old_, destination) == SignalKind::Audio;
    if (!audio)
        return false;  // MIDI is events, and there is no half of a note-on

    if (!beforeRef.valid() || !afterRef.valid()) {
        ++result.unfaded;  // an edge that was connected and is not, or the reverse
        return false;
    }

    // A running fade is kept rather than collapsed. Collapsing one to the side
    // it was heading for is a step of exactly the size the fade had not got
    // through yet, which is the thing this whole pass exists to remove: a
    // second edit arriving four milliseconds into a five millisecond fade would
    // make the biggest click in the session.
    const auto base = fades_[consumer].size();
    std::vector<Fade> emit;
    const auto kept = whole && running && retain(consumer, slot, chain, base, emit);

    if (kept) {
        if (arrived) {
            // The edge arrived where the chain was heading and the chain is
            // still running: it carries on, and the executor adopts every ramp
            // in it because none of their keys or sides moved.
            fades_[consumer].insert(fades_[consumer].end(), emit.begin(), emit.end());
            return false;
        }

        const auto depth =
            crossfadeDepth(old_.ops[static_cast<std::size_t>(chain.front())].key.index) + 1;
        if (canFade(consumer, slot, depth)) {
            Fade stacked;
            stacked.slot = slot;
            stacked.key = fadeKey(consumer, slot, depth);
            stacked.oldSideFade = static_cast<int>(base + emit.size()) - 1;
            stacked.newSide = afterRef;
            emit.push_back(stacked);
            fades_[consumer].insert(fades_[consumer].end(), emit.begin(), emit.end());
            return false;
        }
    }

    if (arrived) {
        // A running chain that could not be kept. The edge is where it was
        // heading, so there is nothing to fade to; it steps by whatever the
        // chain had left to run.
        ++result.unfaded;
        return false;
    }

    // No chain, or one that could not be kept. A spent chain is already
    // outputting the side it was heading for, so fading from there is exact;
    // for a running one this is the step described above, and the only thing
    // left when its ops cannot be reproduced.
    Fade fade;
    fade.slot = slot;
    fade.key = fadeKey(consumer, slot, 0);
    fade.newSide = afterRef;
    if (!canFade(consumer, slot, 0) || !resolveSide(consumer, destination, true, fade.oldSide)) {
        ++result.unfaded;
        return false;
    }

    fades_[consumer].push_back(fade);
    return false;
}

bool Pass::visitConsumer(std::size_t consumer, const PlanOp& before, CrossfadedPlan& result) {
    const auto& after = now_.ops[consumer];

    // An op that gained or lost an edge is not the same sum, and there is no
    // slot-for-slot reading of it: whatever it is now doing it was not doing
    // before. One count for the op rather than one per edge.
    if (before.inputs.size() != after.inputs.size()) {
        for (const auto& input : after.inputs)
            if (kindOf(now_, input) == SignalKind::Audio) {
                ++result.unfaded;
                break;
            }
        return false;
    }

    auto equivalent = true;
    for (std::size_t slot = 0; slot < after.inputs.size(); ++slot)
        equivalent = visitSlot(consumer, before, slot, result) && equivalent;

    return equivalent;
}

RenderPlan Pass::build(int& inserted) const {
    // From the whole struct, so a field added to RenderPlan later travels with
    // the plan rather than being quietly dropped by this pass.
    RenderPlan built = now_;
    built.ops.clear();
    built.outputOps.clear();

    std::vector<OpId> moved(now_.ops.size(), INVALID_OP_ID);

    for (std::size_t i = 0; i < now_.ops.size(); ++i) {
        auto op = now_.ops[i];
        for (auto& input : op.inputs)
            if (input.valid())
                input.op = moved[static_cast<std::size_t>(input.op)];

        // Immediately before the op they feed, which is what keeps the plan in
        // dependency order: every side is either earlier than the consumer or a
        // fade emitted a moment ago.
        std::vector<OpId> emitted;
        for (const auto& fade : fades_[i]) {
            const PortRef oldSide =
                fade.oldSideFade >= 0
                    ? PortRef{emitted[static_cast<std::size_t>(fade.oldSideFade)], 0}
                    : PortRef{moved[static_cast<std::size_t>(fade.oldSide.op)], fade.oldSide.port};
            const PortRef newSide{moved[static_cast<std::size_t>(fade.newSide.op)],
                                  fade.newSide.port};

            PlanOp fadeOp;
            fadeOp.kind = OpKind::Crossfade;
            fadeOp.key = fade.key;
            fadeOp.inputs = {oldSide, newSide};
            fadeOp.outputs = {SignalKind::Audio};
            fadeOp.liveness =
                built.ops[static_cast<std::size_t>(oldSide.op)].liveness == LivenessDomain::Live ||
                        built.ops[static_cast<std::size_t>(newSide.op)].liveness ==
                            LivenessDomain::Live
                    ? LivenessDomain::Live
                    : LivenessDomain::Deterministic;

            emitted.push_back(static_cast<OpId>(built.ops.size()));
            op.inputs[fade.slot] = PortRef{emitted.back(), 0};
            built.ops.push_back(std::move(fadeOp));
            ++inserted;
        }

        moved[i] = static_cast<OpId>(built.ops.size());
        built.ops.push_back(std::move(op));
    }

    for (const auto outputOp : now_.outputOps)
        built.outputOps.push_back(moved[static_cast<std::size_t>(outputOp)]);

    bakeScheduling(built);
    return built;
}

CrossfadedPlan Pass::run() {
    CrossfadedPlan result;

    // One pass, forwards. Everything a decision reads is about ops earlier than
    // the one being decided, which ops being in dependency order is what makes
    // true: the old side of a fade is earlier than its consumer, and so is
    // every input of the op whose unchanged bit is being settled.
    for (std::size_t i = 0; i < now_.ops.size(); ++i) {
        const auto found = oldByKey_.find(now_.ops[i].key);
        if (found == oldByKey_.end())
            continue;  // the edit made this one; whatever it displaced fades downstream

        const auto& before = old_.ops[static_cast<std::size_t>(found->second)];
        const auto carried = diff_.carriedFrom[i] != INVALID_OP_ID;

        // Not the same op at all. Nothing to fade between, and nothing to say
        // about an edge whose consumer changed shape underneath it.
        if (!carried && (before.kind != now_.ops[i].kind || before.outputs != now_.ops[i].outputs))
            continue;

        // A spent fade is a pass-through of the port its consumer now reads
        // directly, so retiring one leaves the consumer computing exactly what
        // it computed. Without that, every retirement would mark its whole
        // downstream changed and an unrelated edit in the same publish would
        // lose its fade to a fade that had already finished.
        const auto equivalent = carried || visitConsumer(i, before, result);
        unchanged_[i] = equivalent && inputsUnchanged(i) ? 1 : 0;
    }

    // Nothing to insert is the common case by far, and the plan the compiler
    // produced is then the plan that is published, byte for byte.
    if (std::ranges::all_of(fades_, [](const auto& list) { return list.empty(); })) {
        result.plan = now_;
        return result;
    }

    result.plan = build(result.inserted);
    return result;
}

}  // namespace

CrossfadedPlan insertCrossfades(const RenderPlan& oldPlan, const RenderPlan& newPlan,
                                const std::vector<char>& stillFading) {
    if (oldPlan.ops.empty())
        return {newPlan, 0, 0};

    return Pass(oldPlan, newPlan, stillFading).run();
}

}  // namespace magda::engine
