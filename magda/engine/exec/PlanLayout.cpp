#include "exec/PlanLayout.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <optional>

namespace magda::engine {
namespace {

/// Producer ops an op waits on, each counted once however many slots it feeds.
std::vector<OpId> distinctProducers(const PlanOp& op) {
    std::vector<OpId> producers;
    producers.reserve(op.inputs.size());
    for (const auto& input : op.inputs) {
        if (!input.valid())
            continue;
        if (std::ranges::find(producers, input.op) == producers.end())
            producers.push_back(input.op);
    }
    return producers;
}

/// A set of ops, one bit each.
class OpSet {
  public:
    void resize(std::size_t numWords) {
        words_.assign(numWords, 0);
    }
    void release() {
        words_.clear();
        words_.shrink_to_fit();
    }
    bool empty() const {
        return words_.empty();
    }
    void add(OpId op) {
        words_[static_cast<std::size_t>(op) / 64] |= 1ULL << (static_cast<std::size_t>(op) % 64);
    }
    void unite(const OpSet& other) {
        if (other.empty())
            return;
        for (std::size_t i = 0; i < words_.size(); ++i)
            words_[i] |= other.words_[i];
    }
    /// Whether every op in this set is also in @p other.
    bool isSubsetOf(const OpSet& other) const {
        if (empty())
            return true;
        for (std::size_t i = 0; i < words_.size(); ++i)
            if ((words_[i] & ~other.words_[i]) != 0)
                return false;
        return true;
    }

  private:
    std::vector<std::uint64_t> words_;
};

/// The input slot an op's first output port may be written over, when nothing
/// else still needs what that slot holds.
///
/// Every op here already treats its input as scratch it may overwrite, so this
/// only removes a copy; it never changes what is computed. MIDI is absent on
/// purpose: a MIDI op clears its output before writing it, so an output sharing
/// its input's buffer would clear the events it was about to read.
std::optional<std::size_t> inPlaceInputOf(const PlanOp& op) {
    if (op.outputs.empty() || op.outputs.front().kind != SignalKind::Audio)
        return std::nullopt;

    switch (op.kind) {
        // A device is handed its input in the buffer it writes its output to,
        // which is the contract it already has; sharing means the copy that
        // sets that up is a copy the buffer already holds.
        case OpKind::Device:
        case OpKind::Delay:
        case OpKind::Gain:
        case OpKind::SendTap:
        case OpKind::Meter:
        case OpKind::Fader:
        // A difference is taken in the buffer holding the signal it measures.
        // The dry side it subtracts is a different port, and one op reading a
        // port is what lets that port be written over at all.
        case OpKind::Subtract:
            return op.inputs.empty() || !op.inputs.front().valid() ? std::nullopt
                                                                   : std::optional<std::size_t>(0);

        // The side the edge is becoming, never the side it was. A fade reads
        // both at an index before it writes it, so writing over the new side
        // costs nothing; writing over the old one would too, but the new side
        // is the buffer the fade's output goes on being once it is spent.
        case OpKind::Crossfade:
            return op.inputs.size() < 2 || !op.inputs[1].valid() ? std::nullopt
                                                                 : std::optional<std::size_t>(1);

        // A sum can accumulate into the first thing it sums, which is what it
        // would have copied there anyway.
        case OpKind::MixAudio:
            for (std::size_t slot = 0; slot < op.inputs.size(); ++slot)
                if (op.inputs[slot].valid())
                    return slot;
            return std::nullopt;

        case OpKind::ClipAudio:
        case OpKind::ClipMidi:
        case OpKind::AudioInput:
        case OpKind::MidiInput:
        case OpKind::MergeMidi:
        case OpKind::MidiNoteGate:
        case OpKind::ModSource:
        case OpKind::Output:
            return std::nullopt;
    }

    return std::nullopt;
}

}  // namespace

std::vector<int> portOffsetsOf(const RenderPlan& plan) {
    std::vector<int> offsets(plan.ops.size() + 1, 0);
    int total = 0;
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        offsets[i] = total;
        total += static_cast<int>(plan.ops[i].outputs.size());
    }
    offsets[plan.ops.size()] = total;
    return offsets;
}

PlanLatency resolvePlanLatency(const RenderPlan& plan, const std::vector<int>& portOffsets,
                               const std::vector<int>& deviceLatency) {
    PlanLatency resolved;
    resolved.delaySamples.assign(plan.ops.size(), 0);
    resolved.portLatency.assign(static_cast<std::size_t>(portOffsets.back()), 0);

    const auto flat = [&portOffsets](const PortRef& ref) {
        return static_cast<std::size_t>(portOffsets[static_cast<std::size_t>(ref.op)] + ref.port);
    };

    // What an input carries before its delay compensates anything. A delay op
    // is transparent here: its own count is what this pass is computing, so
    // the port behind it is what the comparison is between.
    const auto arriving = [&](const PortRef& input) {
        const auto& producer = plan.ops[static_cast<std::size_t>(input.op)];
        return producer.kind == OpKind::Delay ? resolved.portLatency[flat(producer.inputs.front())]
                                              : resolved.portLatency[flat(input)];
    };

    // Ops are in dependency order, so everything an op reads is already
    // resolved by the time the walk reaches it. The delays feeding an op are
    // resolved from it rather than at their own index, because what they have
    // to match is the longest path into the op they feed, and only the op
    // knows every path.
    for (std::size_t i = 0; i < plan.ops.size(); ++i) {
        const auto& op = plan.ops[i];
        if (op.kind == OpKind::Delay)
            continue;

        int target = 0;
        for (const auto& input : op.inputs)
            if (input.valid())
                target = std::max(target, arriving(input));

        for (const auto& input : op.inputs) {
            if (!input.valid())
                continue;
            const auto delayOp = static_cast<std::size_t>(input.op);
            if (plan.ops[delayOp].kind != OpKind::Delay)
                continue;
            resolved.delaySamples[delayOp] = target - arriving(input);
            resolved.portLatency[flat(PortRef{input.op, 0})] = target;
        }

        // A fade produces the latency of the side it is becoming, not the
        // longest of the two. A fade that was refused a ramp outputs only the
        // new side, a spent one is a pass-through of it, and a running one has
        // already been told its two sides arrive together; taking the maximum
        // would misalign every parallel path downstream for as long as an old
        // side that happened to be more latent stayed in the plan.
        const auto produced =
            op.kind == OpKind::Crossfade && op.inputs.size() > 1 && op.inputs[1].valid()
                ? arriving(op.inputs[1])
                : target + (op.kind == OpKind::Device ? std::max(0, deviceLatency[i]) : 0);
        for (std::size_t port = 0; port < op.outputs.size(); ++port)
            resolved.portLatency[static_cast<std::size_t>(portOffsets[i]) + port] = produced;

        if (op.kind == OpKind::Output)
            resolved.outputLatency = std::max(resolved.outputLatency, target);
    }

    return resolved;
}

BufferLayout assignBuffers(const RenderPlan& plan, const std::vector<int>& portOffsets,
                           const std::vector<int>& delaySamples) {
    const auto numOps = plan.ops.size();
    const auto numPorts = static_cast<std::size_t>(portOffsets.back());
    const auto numWords = (numOps + 63) / 64;

    BufferLayout layout;
    layout.portSlots.assign(numPorts, -1);
    layout.writesInPlace.assign(numOps, 0);
    layout.elided.assign(numOps, 0);

    const auto flat = [&portOffsets](const PortRef& ref) {
        return static_cast<std::size_t>(portOffsets[static_cast<std::size_t>(ref.op)] + ref.port);
    };

    // A delay holding no samples writes exactly what it read, so its output is
    // its input's port rather than a copy of it and the op never runs. This is
    // what makes a plan compiled for compensation cost nothing where there is
    // none to do: the ops are there, and where the latency is zero so is the
    // work, the buffer and the copy.
    std::vector<std::size_t> canonical(numPorts);
    std::iota(canonical.begin(), canonical.end(), 0);
    for (std::size_t i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[i];
        if (op.kind != OpKind::Delay || delaySamples[i] != 0)
            continue;
        layout.elided[i] = 1;
        canonical[static_cast<std::size_t>(portOffsets[i])] = canonical[flat(op.inputs.front())];
    }

    // Who reads each port, by the port it resolves to. Elided delays are left
    // out: they do not run, so nothing waits on them and nothing stays alive
    // for them. That is also what lets the op behind one work in place.
    std::vector<std::vector<OpId>> readers(numPorts);
    for (std::size_t i = 0; i < numOps; ++i) {
        if (layout.elided[i])
            continue;
        for (const auto& input : plan.ops[i].inputs)
            if (input.valid())
                readers[canonical[flat(input)]].push_back(static_cast<OpId>(i));
    }

    // Ops that must have finished before each op can start. Held only while
    // something downstream still needs it, so what is resident at any point is
    // the width of the graph rather than its size.
    std::vector<OpSet> ancestors(numOps);
    std::vector<int> unreadOutputs(numOps, 0);
    for (std::size_t i = 0; i < numOps; ++i)
        for (const auto producer : distinctProducers(plan.ops[i]))
            ++unreadOutputs[static_cast<std::size_t>(producer)];

    // One keep-alive set per slot: everything that has to be done with the slot
    // before it can be handed to another port.
    std::vector<OpSet> audioSlotUsers, midiSlotUsers;

    for (std::size_t i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[i];
        const auto producers = distinctProducers(op);

        auto& mine = ancestors[i];
        mine.resize(numWords);
        for (const auto producer : producers) {
            mine.add(producer);
            mine.unite(ancestors[static_cast<std::size_t>(producer)]);
        }

        const auto inPlaceSlot = layout.elided[i] ? std::nullopt : inPlaceInputOf(op);

        for (std::size_t port = 0; port < op.outputs.size(); ++port) {
            const auto flatPort = static_cast<std::size_t>(portOffsets[i]) + port;
            if (canonical[flatPort] != flatPort)
                continue;  // elided: it is its input's port and shares its slot

            const auto isAudio = op.outputs[port].kind == SignalKind::Audio;
            auto& slotUsers = isAudio ? audioSlotUsers : midiSlotUsers;

            // Writing over an input the op is the last to read costs nothing
            // and saves both the buffer and the copy that would fill it. "Last
            // to read" has to mean last in every schedule, and one op reading
            // the port once is the only form of that which needs no ordering
            // argument at all.
            int slot = -1;
            if (port == 0 && inPlaceSlot.has_value()) {
                const auto source = canonical[flat(op.inputs[*inPlaceSlot])];
                const auto& sourceReaders = readers[source];
                if (sourceReaders.size() == 1 && sourceReaders.front() == static_cast<OpId>(i) &&
                    layout.portSlots[source] >= 0) {
                    slot = layout.portSlots[source];
                    layout.writesInPlace[i] = 1;
                }
            }

            if (slot < 0) {
                for (std::size_t candidate = 0; candidate < slotUsers.size(); ++candidate) {
                    if (slotUsers[candidate].isSubsetOf(mine)) {
                        slot = static_cast<int>(candidate);
                        break;
                    }
                }
            }

            if (slot < 0) {
                slot = static_cast<int>(slotUsers.size());
                slotUsers.emplace_back();
                slotUsers.back().resize(numWords);
            }

            layout.portSlots[flatPort] = slot;

            // An output nobody reads still owns its buffer while it is written.
            if (readers[flatPort].empty())
                slotUsers[static_cast<std::size_t>(slot)].add(static_cast<OpId>(i));
            for (const auto reader : readers[flatPort])
                slotUsers[static_cast<std::size_t>(slot)].add(reader);
        }

        for (const auto producer : producers)
            if (--unreadOutputs[static_cast<std::size_t>(producer)] == 0)
                ancestors[static_cast<std::size_t>(producer)].release();
    }

    for (std::size_t flatPort = 0; flatPort < numPorts; ++flatPort)
        layout.portSlots[flatPort] = layout.portSlots[canonical[flatPort]];

    layout.numAudioSlots = static_cast<int>(audioSlotUsers.size());
    layout.numMidiSlots = static_cast<int>(midiSlotUsers.size());
    return layout;
}

PreparedLayout resolveLayout(const RenderPlan& plan, const std::vector<int>& deviceLatency) {
    PreparedLayout prepared;
    prepared.portOffsets = portOffsetsOf(plan);
    prepared.latency = resolvePlanLatency(plan, prepared.portOffsets, deviceLatency);
    prepared.buffers = assignBuffers(plan, prepared.portOffsets, prepared.latency.delaySamples);
    return prepared;
}

}  // namespace magda::engine
