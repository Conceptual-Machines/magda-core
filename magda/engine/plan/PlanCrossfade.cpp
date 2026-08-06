#include "plan/PlanCrossfade.hpp"

#include <algorithm>
#include <map>

#include "plan/PlanDiff.hpp"

namespace magda::engine {
namespace {

/// How far an edge may be followed through fades before something is wrong.
/// A fade never reads a fade, so one hop is the real bound; the rest is so a
/// malformed plan cannot make this a loop.
constexpr int kMaxFadeHops = 8;

/// The edge behind a fade, taking @p slot at every one. Anything else is
/// returned as it came, so this is the identity on an ordinary edge.
PortRef throughFades(const RenderPlan& plan, PortRef ref, std::size_t slot) {
    for (int hop = 0; hop < kMaxFadeHops; ++hop) {
        if (!ref.valid())
            return ref;
        const auto& op = plan.ops[static_cast<std::size_t>(ref.op)];
        if (op.kind != OpKind::Crossfade || slot >= op.inputs.size())
            return ref;
        ref = op.inputs[slot];
    }
    return ref;
}

/// The signal a port carries, or Midi where there is no port to ask.
SignalKind kindOf(const RenderPlan& plan, const PortRef& ref) {
    if (!ref.valid())
        return SignalKind::Midi;
    const auto& outputs = plan.ops[static_cast<std::size_t>(ref.op)].outputs;
    const auto port = static_cast<std::size_t>(ref.port);
    return port < outputs.size() ? outputs[port] : SignalKind::Midi;
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

/// One fade to put in front of one op: which slot it fills, and where the old
/// side is in the new plan.
struct Insertion {
    std::size_t slot = 0;
    PortRef oldSide;
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
          insertions_(newPlan.ops.size()),
          unchanged_(newPlan.ops.size(), 0) {}

    CrossfadedPlan run();

  private:
    /// Whether the op the old plan holds at @p ref was faded and has not
    /// finished. Ops of the old plan, so the caller's vector indexes straight.
    bool isRunningFade(const PortRef& ref) const {
        if (!ref.valid() || old_.ops[static_cast<std::size_t>(ref.op)].kind != OpKind::Crossfade)
            return false;
        const auto op = static_cast<std::size_t>(ref.op);
        return op < stillFading_.size() && stillFading_[op] != 0;
    }

    /// Decide one op's slots. Returns whether the op is computing what it
    /// computed before, which is the seed the forward pass needs.
    bool visitConsumer(std::size_t consumer, const PlanOp& before, CrossfadedPlan& result);

    /// Put a fade on @p slot of @p consumer, fading from the edge @p oldSide
    /// names in the old plan. Returns false when any of the conditions in
    /// PlanCrossfade.hpp fails, and nothing is recorded.
    bool insert(std::size_t consumer, std::size_t slot, const PortRef& oldSide);

    /// Every input of an op that already counts as unchanged in itself.
    bool inputsUnchanged(std::size_t op) const {
        for (const auto& input : now_.ops[op].inputs)
            if (input.valid() && unchanged_[static_cast<std::size_t>(input.op)] == 0)
                return false;
        return true;
    }

    RenderPlan build(int& inserted) const;

    const RenderPlan& old_;
    const RenderPlan& now_;
    const std::vector<char>& stillFading_;
    PlanDiff diff_;
    std::map<OpKey, OpId> oldByKey_, newByKey_;
    std::vector<std::vector<Insertion>> insertions_;

    /// Per op of the new plan: it carried, and so did everything it reads. The
    /// only thing a fade may take as its old side.
    std::vector<char> unchanged_;
};

bool Pass::insert(std::size_t consumer, std::size_t slot, const PortRef& oldSide) {
    const auto& after = now_.ops[consumer];
    if (!oldSide.valid() || slot >= after.inputs.size() || !after.inputs[slot].valid())
        return false;

    // A delay's count is read off the fan-in it feeds. A fade in front of one
    // would be resolved against the fade instead, and a fade behind one would
    // put the delay where the fan-in cannot see it: neither is a fade, both are
    // a compensation bug that only shows up on a session with latency in it.
    if (after.kind == OpKind::Delay)
        return false;
    if (now_.ops[static_cast<std::size_t>(after.inputs[slot].op)].kind == OpKind::Delay)
        return false;

    // The key is one integer holding two numbers, and a slot past the stride
    // would land in the next role's band.
    if (slot >= static_cast<std::size_t>(kCrossfadeRoleStride))
        return false;

    // The old side has to still be in the plan, and still be where the fade can
    // read it: ops are in dependency order, so one integer comparison is also
    // the whole of the cycle check.
    const auto producer = newByKey_.find(old_.ops[static_cast<std::size_t>(oldSide.op)].key);
    if (producer == newByKey_.end() || producer->second >= static_cast<OpId>(consumer))
        return false;

    const auto oldSideOp = static_cast<std::size_t>(producer->second);
    const auto& producerOp = now_.ops[oldSideOp];
    if (producerOp.kind == OpKind::Delay || producerOp.kind == OpKind::Crossfade)
        return false;

    const auto port = static_cast<std::size_t>(oldSide.port);
    if (port >= producerOp.outputs.size() || producerOp.outputs[port] != SignalKind::Audio)
        return false;

    // What it is computing, not only that it is still there. A producer whose
    // own inputs moved hands back a signal that was never the old one.
    if (unchanged_[oldSideOp] == 0)
        return false;

    // Liveness travels downstream and validatePlan checks it both ways, so a
    // fade cannot be the thing that puts a live signal in front of an op the
    // anticipative executor is allowed to run early.
    if (producerOp.liveness == LivenessDomain::Live &&
        after.liveness == LivenessDomain::Deterministic)
        return false;

    insertions_[consumer].push_back({slot, PortRef{producer->second, oldSide.port}});
    return true;
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

    for (std::size_t slot = 0; slot < after.inputs.size(); ++slot) {
        const auto beforeRef = before.inputs[slot];
        const auto afterRef = after.inputs[slot];

        // A fade already on this edge is not the old signal, it is the pair of
        // signals it is between. What the edge was heading for is the old side
        // of anything that replaces it, so fades collapse rather than stack.
        const auto wasHeadingFor = throughFades(old_, beforeRef, 1);
        const auto audio = kindOf(now_, afterRef) == SignalKind::Audio ||
                           kindOf(old_, wasHeadingFor) == SignalKind::Audio;

        if (sameEdge(old_, wasHeadingFor, now_, afterRef)) {
            if (!isRunningFade(beforeRef))
                continue;  // either nothing moved, or a spent fade is retiring

            // The edge arrived where the fade was heading and the fade is still
            // running, so it carries on: same key, same sides, and the executor
            // adopts the ramp where it had got to.
            equivalent = false;
            const auto& fade = old_.ops[static_cast<std::size_t>(beforeRef.op)];
            if (!insert(consumer, slot, throughFades(old_, fade.inputs.front(), 0)) && audio)
                ++result.unfaded;
            continue;
        }

        equivalent = false;
        if (!audio)
            continue;  // MIDI is events, and there is no half of a note-on

        if (!beforeRef.valid() || !afterRef.valid()) {
            ++result.unfaded;  // an edge that was connected and is not, or the reverse
            continue;
        }

        if (!insert(consumer, slot, wasHeadingFor))
            ++result.unfaded;
    }

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

        // Immediately before the op it feeds, which is what keeps the plan in
        // dependency order: its old side is earlier than the consumer and its
        // new side is what the consumer was already reading.
        for (const auto& insertion : insertions_[i]) {
            const PortRef oldSide{moved[static_cast<std::size_t>(insertion.oldSide.op)],
                                  insertion.oldSide.port};
            const auto newSide = op.inputs[insertion.slot];

            PlanOp fade;
            fade.kind = OpKind::Crossfade;
            fade.key = op.key;
            fade.key.role = OpRole::EdgeCrossfade;
            fade.key.index = crossfadeIndex(now_.ops[i].key.role, static_cast<int>(insertion.slot));
            fade.inputs = {oldSide, newSide};
            fade.outputs = {SignalKind::Audio};
            fade.liveness =
                built.ops[static_cast<std::size_t>(oldSide.op)].liveness == LivenessDomain::Live ||
                        built.ops[static_cast<std::size_t>(newSide.op)].liveness ==
                            LivenessDomain::Live
                    ? LivenessDomain::Live
                    : LivenessDomain::Deterministic;

            op.inputs[insertion.slot] = PortRef{static_cast<OpId>(built.ops.size()), 0};
            built.ops.push_back(std::move(fade));
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
    if (std::ranges::all_of(insertions_, [](const auto& list) { return list.empty(); })) {
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
