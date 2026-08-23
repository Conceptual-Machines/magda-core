#include "plan/PlanDiff.hpp"

#include <map>

namespace magda::engine {
namespace {

/// Whether two ops compute the same thing, given that their keys already match.
bool sameShape(const PlanOp& before, const PlanOp& after) {
    return before.kind == after.kind && before.outputs == after.outputs &&
           before.inputs.size() == after.inputs.size() &&
           before.audioInputChannels == after.audioInputChannels;
}

/// Whether both ops read the same places. Producers are compared by key rather
/// than by index, because an op's index says nothing across a recompile: an
/// edit anywhere earlier in the plan moves every op after it.
bool sameInputs(const RenderPlan& oldPlan, const PlanOp& before, const RenderPlan& newPlan,
                const PlanOp& after) {
    for (std::size_t slot = 0; slot < before.inputs.size(); ++slot) {
        const auto& from = before.inputs[slot];
        const auto& to = after.inputs[slot];

        if (from.valid() != to.valid())
            return false;
        if (!from.valid())
            continue;
        if (from.port != to.port)
            return false;
        if (oldPlan.ops[static_cast<std::size_t>(from.op)].key !=
            newPlan.ops[static_cast<std::size_t>(to.op)].key)
            return false;
    }

    return true;
}

}  // namespace

PlanDiff diffPlans(const RenderPlan& oldPlan, const RenderPlan& newPlan) {
    PlanDiff diff;
    diff.carriedFrom.assign(newPlan.ops.size(), INVALID_OP_ID);

    // Keys are unique in a plan validatePlan has passed, which is what makes
    // this a join rather than a search.
    std::map<OpKey, OpId> oldByKey;
    for (std::size_t i = 0; i < oldPlan.ops.size(); ++i)
        oldByKey.emplace(oldPlan.ops[i].key, static_cast<OpId>(i));

    std::vector<char> adopted(oldPlan.ops.size(), 0);

    for (std::size_t i = 0; i < newPlan.ops.size(); ++i) {
        const auto& after = newPlan.ops[i];
        const auto found = oldByKey.find(after.key);
        if (found == oldByKey.end())
            continue;

        const auto& before = oldPlan.ops[static_cast<std::size_t>(found->second)];
        if (!sameShape(before, after) || !sameInputs(oldPlan, before, newPlan, after))
            continue;

        diff.carriedFrom[i] = found->second;
        adopted[static_cast<std::size_t>(found->second)] = 1;
        ++diff.carried;
    }

    for (std::size_t i = 0; i < oldPlan.ops.size(); ++i)
        if (!adopted[i])
            diff.retired.push_back(static_cast<OpId>(i));

    return diff;
}

}  // namespace magda::engine
