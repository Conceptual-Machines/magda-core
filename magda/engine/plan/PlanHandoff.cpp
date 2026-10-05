#include "plan/PlanHandoff.hpp"

#include <map>
#include <utility>

namespace magda::engine {

namespace {

/// Whether @p producer feeds @p consumer across the boundary without a handoff.
bool crossesUnguarded(const PlanOp& consumer, const PlanOp& producer) {
    return runsAtCallback(consumer) && !runsAtCallback(producer) &&
           producer.kind != OpKind::Handoff;
}

/// @p key with what is particular to its op cleared: where it sits, not what it is there.
OpKey locationOf(OpKey key) {
    key.role = OpRole::Handoff;
    key.index = 0;
    return key;
}

}  // namespace

bool runsAtCallback(const PlanOp& op) {
    return op.liveness == LivenessDomain::Live || op.kind == OpKind::Output ||
           op.kind == OpKind::InsertSend;
}

RenderPlan insertHandoffs(const RenderPlan& plan) {
    if (handoffProblems(plan).empty())
        return plan;

    // From the whole struct, so a field added to RenderPlan later travels with the plan.
    RenderPlan built = plan;
    built.ops.clear();
    built.outputOps.clear();

    std::vector<OpId> moved(plan.ops.size(), INVALID_OP_ID);
    std::map<std::pair<OpId, int>, OpId> handoffs;
    std::map<OpKey, int> perLocation;

    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        auto op = plan.ops[i];
        for (auto& input : op.inputs) {
            if (!input.valid())
                continue;
            const auto& producer = plan.ops[static_cast<std::size_t>(input.op)];
            if (!crossesUnguarded(op, producer)) {
                input.op = moved[static_cast<std::size_t>(input.op)];
                continue;
            }

            const auto [found, inserted] = handoffs.try_emplace({input.op, input.port});
            if (inserted) {
                PlanOp handoff;
                handoff.kind = OpKind::Handoff;
                handoff.key = locationOf(producer.key);
                handoff.key.index = perLocation[handoff.key]++;
                handoff.liveness = LivenessDomain::Deterministic;
                handoff.inputs = {PortRef{moved[static_cast<std::size_t>(input.op)], input.port}};
                handoff.outputs = {
                    PortDesc{producer.outputs[static_cast<std::size_t>(input.port)].kind}};
                found->second = static_cast<OpId>(built.ops.size());
                built.ops.push_back(std::move(handoff));
            }
            input = PortRef{found->second, 0};
        }

        moved[i] = static_cast<OpId>(built.ops.size());
        built.ops.push_back(std::move(op));
    }

    for (const auto outputOp : plan.outputOps)
        built.outputOps.push_back(moved[static_cast<std::size_t>(outputOp)]);

    bakeScheduling(built);
    return built;
}

std::vector<std::string> handoffProblems(const RenderPlan& plan) {
    std::vector<std::string> problems;
    const auto numOps = static_cast<OpId>(plan.ops.size());
    for (OpId i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[static_cast<std::size_t>(i)];
        for (const auto& input : op.inputs) {
            if (!input.valid() || input.op < 0 || input.op >= numOps)
                continue;
            if (crossesUnguarded(op, plan.ops[static_cast<std::size_t>(input.op)]))
                problems.push_back("op " + std::to_string(i) + " (" + toString(op.kind) + " " +
                                   toString(op.key) + ") runs at the callback and reads op " +
                                   std::to_string(input.op) + " without a handoff");
        }
    }
    return problems;
}

}  // namespace magda::engine
