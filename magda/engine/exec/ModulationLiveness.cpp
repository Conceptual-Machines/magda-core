#include "exec/ModulationLiveness.hpp"

#include <map>
#include <numeric>

#include "param/ModRuntime.hpp"
#include "plan/PlanHandoff.hpp"

namespace magda::engine {

namespace {

/// Parameters first, then modifiers, in one index space.
class Components {
  public:
    explicit Components(int size) : parent_(static_cast<std::size_t>(size)) {
        std::iota(parent_.begin(), parent_.end(), 0);
    }

    int find(int node) {
        while (parent_[static_cast<std::size_t>(node)] != node) {
            auto& up = parent_[static_cast<std::size_t>(node)];
            up = parent_[static_cast<std::size_t>(up)];
            node = up;
        }
        return node;
    }

    void join(int a, int b) {
        parent_[static_cast<std::size_t>(find(a))] = find(b);
    }

  private:
    std::vector<int> parent_;
};

bool isLive(const PlanOp& op) {
    return op.liveness == LivenessDomain::Live;
}

/// Liveness downstream of what is already live, carries included; true when anything moved.
bool propagate(RenderPlan& plan) {
    // A return is live when what its send fills it with is.
    std::map<OpId, OpId> filledBy;
    for (const auto& op : plan.ops)
        if (op.kind == OpKind::FeedbackSend && op.inputs.size() > 1 && op.inputs[0].valid() &&
            op.inputs[1].valid())
            filledBy[op.inputs[1].op] = op.inputs[0].op;

    bool changed = false;
    for (bool moved = true; moved;) {
        moved = false;
        for (std::size_t i = 0; i < plan.ops.size(); ++i) {
            auto& op = plan.ops[i];
            if (isLive(op))
                continue;
            bool live = false;
            for (const auto& input : op.inputs)
                live =
                    live || (input.valid() && isLive(plan.ops[static_cast<std::size_t>(input.op)]));
            if (const auto found = filledBy.find(static_cast<OpId>(i)); found != filledBy.end())
                live = live || isLive(plan.ops[static_cast<std::size_t>(found->second)]);
            if (live) {
                op.liveness = LivenessDomain::Live;
                moved = changed = true;
            }
        }
    }
    return changed;
}

/// The modifiers ModSource @p op feeds, as the executor picks them: an audio listener at the
/// tap it names, a MIDI one at the pre-FX tap that carries the notes.
std::vector<int> listenersOf(const PlanOp& op, const ParamTable& table) {
    std::vector<int> listeners;
    if (op.kind != OpKind::ModSource)
        return listeners;
    for (int modifier = 0; modifier < static_cast<int>(table.modifiers.size()); ++modifier) {
        const auto& mod = table.modifiers[static_cast<std::size_t>(modifier)];
        if (mod.source != op.key.trackId)
            continue;
        const auto listen = ModRuntime::listensFor(modifier, table);
        const auto index = listen == ModListen::Audio
                               ? (mod.tap == magda::ModTapPoint::PostFader ? 1 : 0)
                           : listen == ModListen::Midi ? 0
                                                       : -1;
        if (index == op.key.index)
            listeners.push_back(modifier);
    }
    return listeners;
}

}  // namespace

std::vector<ParamId> paramsReadBy(const PlanOp& op, const ParamTable& table) {
    std::vector<ParamId> params;
    const auto add = [&params](ParamId id) {
        if (id != INVALID_PARAM_ID)
            params.push_back(id);
    };

    if (op.kind == OpKind::Device) {
        const auto window = table.windowFor(op.key.deviceKey());
        for (int k = 0; k < window.count; ++k)
            params.push_back(window.first + k);
        return params;
    }

    ParamKey key;
    key.scope = ParamKey::Scope::Track;
    key.trackId = op.key.trackId;
    if (op.kind == OpKind::Fader && op.key.role == OpRole::TrackFader) {
        key.kind = ParamKey::Kind::TrackVolume;
        add(table.find(key));
        key.kind = ParamKey::Kind::TrackPan;
        add(table.find(key));
    } else if (op.kind == OpKind::Fader && op.key.role == OpRole::RackChainFader &&
               op.padLevelParam >= 0) {
        add(table.deviceParam(op.key.deviceKey(), op.padLevelParam));
        add(table.deviceParam(op.key.deviceKey(), op.padPanParam));
    } else if (op.kind == OpKind::SendTap) {
        key.kind = ParamKey::Kind::SendLevel;
        key.index = op.key.index;
        add(table.find(key));
    }
    return params;
}

bool promoteModulatedLiveness(RenderPlan& plan, const ParamTable& table) {
    const auto numParams = table.size();
    const auto numModifiers = static_cast<int>(table.modifiers.size());
    Components components(numParams + numModifiers);

    for (ParamId param = 0; param < numParams; ++param)
        for (const auto& link : table.linksFor(param))
            components.join(param, link.source.kind == ParamSourceRef::Kind::Modifier
                                       ? numParams + link.source.index
                                       : link.source.index);
    for (int modifier = 0; modifier < numModifiers; ++modifier)
        if (const auto rate = table.modifiers[static_cast<std::size_t>(modifier)].rate;
            rate != INVALID_PARAM_ID)
            components.join(numParams + modifier, rate);

    // An op's nodes: the parameters it reads, or for a tap the modifiers it feeds, which share
    // its trigger detector and so its side.
    std::vector<std::vector<int>> nodesOf(plan.ops.size());
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        nodesOf[i] = paramsReadBy(plan.ops[i], table);
        for (const auto listener : listenersOf(plan.ops[i], table))
            nodesOf[i].push_back(numParams + listener);
        for (std::size_t k = 1; k < nodesOf[i].size(); ++k)
            components.join(nodesOf[i][0], nodesOf[i][k]);
    }

    bool changed = false;
    for (bool moved = true; moved;) {
        moved = false;

        // What makes a component live: anything in it touched by an op the callback runs, a
        // live tap feeding its modifiers included.
        std::vector<char> live(static_cast<std::size_t>(numParams + numModifiers), 0);
        for (std::size_t i = 0; i < plan.ops.size(); ++i)
            if (runsAtCallback(plan.ops[i]))
                for (const auto node : nodesOf[i])
                    live[static_cast<std::size_t>(components.find(node))] = 1;

        for (std::size_t i = 0; i < plan.ops.size(); ++i) {
            auto& op = plan.ops[i];
            if (isLive(op))
                continue;
            for (const auto node : nodesOf[i])
                if (live[static_cast<std::size_t>(components.find(node))] != 0) {
                    op.liveness = LivenessDomain::Live;
                    op.liveByModulation = true;
                    moved = true;
                    break;
                }
        }
        moved = propagate(plan) || moved;
        changed = changed || moved;
    }
    return changed;
}

}  // namespace magda::engine
