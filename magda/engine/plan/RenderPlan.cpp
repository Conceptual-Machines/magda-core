#include "plan/RenderPlan.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace magda::engine {

bool OpKey::operator<(const OpKey& o) const {
    return std::tie(trackId, rackId, chainId, deviceId, role, index) <
           std::tie(o.trackId, o.rackId, o.chainId, o.deviceId, o.role, o.index);
}

int arityOf(OpKind kind) {
    switch (kind) {
        case OpKind::ClipAudio:
        case OpKind::ClipMidi:
        case OpKind::AudioInput:
        case OpKind::MidiInput:
            return 0;
        case OpKind::Device:
            return 3;  // audio, MIDI, sidechain audio
        case OpKind::Fader:
            return 2;  // audio, MIDI
        case OpKind::Crossfade:
            return 2;  // the edge as it was, the edge as it is
        case OpKind::MixAudio:
        case OpKind::MergeMidi:
            return -1;  // variadic
        case OpKind::Delay:
        case OpKind::Gain:
        case OpKind::SendTap:
        case OpKind::Meter:
        case OpKind::Output:
            return 1;
    }
    return -1;
}

const char* toString(OpKind kind) {
    switch (kind) {
        case OpKind::ClipAudio:
            return "ClipAudio";
        case OpKind::ClipMidi:
            return "ClipMidi";
        case OpKind::AudioInput:
            return "AudioInput";
        case OpKind::MidiInput:
            return "MidiInput";
        case OpKind::Device:
            return "Device";
        case OpKind::MixAudio:
            return "MixAudio";
        case OpKind::MergeMidi:
            return "MergeMidi";
        case OpKind::Delay:
            return "Delay";
        case OpKind::Crossfade:
            return "Crossfade";
        case OpKind::Gain:
            return "Gain";
        case OpKind::Fader:
            return "Fader";
        case OpKind::SendTap:
            return "SendTap";
        case OpKind::Meter:
            return "Meter";
        case OpKind::Output:
            return "Output";
    }
    return "?";
}

const char* toString(OpRole role) {
    switch (role) {
        case OpRole::ClipAudio:
            return "clipAudio";
        case OpRole::ClipMidi:
            return "clipMidi";
        case OpRole::LiveAudioInput:
            return "liveAudioInput";
        case OpRole::LiveMidiInput:
            return "liveMidiInput";
        case OpRole::TrackAudioInput:
            return "trackAudioInput";
        case OpRole::TrackMidiInput:
            return "trackMidiInput";
        case OpRole::DeviceProcess:
            return "deviceProcess";
        case OpRole::DeviceGain:
            return "deviceGain";
        case OpRole::DeviceMeter:
            return "deviceMeter";
        case OpRole::ChainMidiMerge:
            return "chainMidiMerge";
        case OpRole::RackChainFader:
            return "rackChainFader";
        case OpRole::RackMix:
            return "rackMix";
        case OpRole::RackMidiMix:
            return "rackMidiMix";
        case OpRole::RackFader:
            return "rackFader";
        case OpRole::TrackFader:
            return "trackFader";
        case OpRole::TrackMeter:
            return "trackMeter";
        case OpRole::TrackMute:
            return "trackMute";
        case OpRole::SendTap:
            return "sendTap";
        case OpRole::HardwareOutput:
            return "hardwareOutput";
        case OpRole::MixInputDelay:
            return "mixInputDelay";
        case OpRole::MergeInputDelay:
            return "mergeInputDelay";
        case OpRole::DeviceInputDelay:
            return "deviceInputDelay";
        case OpRole::FaderInputDelay:
            return "faderInputDelay";
        case OpRole::EdgeCrossfade:
            return "edgeCrossfade";
    }
    return "?";
}

const char* toString(SignalKind kind) {
    return kind == SignalKind::Audio ? "audio" : "midi";
}

const char* toString(LivenessDomain domain) {
    return domain == LivenessDomain::Live ? "live" : "det";
}

std::string toString(const OpKey& key) {
    std::string out;
    if (key.trackId != INVALID_TRACK_ID)
        out += "T" + std::to_string(key.trackId);
    if (key.rackId != INVALID_RACK_ID)
        out += "/R" + std::to_string(key.rackId);
    if (key.chainId != INVALID_CHAIN_ID)
        out += "/C" + std::to_string(key.chainId);
    if (key.deviceId != INVALID_DEVICE_ID)
        out += "/D" + std::to_string(key.deviceId);
    out += ":";
    out += toString(key.role);

    // A fade's index is four numbers packed into one, and printing the packing
    // would put "#117441536" in front of anyone reading a dump. What it stands
    // for is the edge the fade sits on, so that is what comes out.
    if (key.role == OpRole::EdgeCrossfade) {
        const auto consumerIndex = crossfadeConsumerIndex(key.index);
        const auto depth = crossfadeDepth(key.index);
        out += "(";
        out += toString(crossfadeConsumerRole(key.index));
        if (consumerIndex != 0)
            out += "#" + std::to_string(consumerIndex);
        out += " slot " + std::to_string(crossfadeSlot(key.index));
        if (depth != 0)
            out += " over " + std::to_string(depth);
        out += ")";
        return out;
    }

    if (key.index != 0)
        out += "#" + std::to_string(key.index);
    return out;
}

namespace {

/// Whether a role names a delay on one input slot of the op it is keyed to.
bool isInputDelayRole(OpRole role) {
    switch (role) {
        case OpRole::MixInputDelay:
        case OpRole::MergeInputDelay:
        case OpRole::DeviceInputDelay:
        case OpRole::FaderInputDelay:
            return true;
        default:
            return false;
    }
}

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

}  // namespace

PlanScheduling scheduleOf(const RenderPlan& plan) {
    const auto numOps = plan.ops.size();

    PlanScheduling schedule;
    schedule.dependencyCounts.assign(numOps, 0);
    schedule.consumerOffsets.assign(numOps + 1, 0);

    // Pass 1: dependency counts, and per-producer consumer counts into the
    // offset array (shifted by one so the prefix sum below lands in place).
    for (std::size_t i = 0; i < numOps; ++i) {
        const auto producers = distinctProducers(plan.ops[i]);
        schedule.dependencyCounts[i] = static_cast<std::uint16_t>(producers.size());
        if (producers.empty())
            schedule.initialReadyOps.push_back(static_cast<OpId>(i));
        for (const auto producer : producers)
            ++schedule.consumerOffsets[static_cast<std::size_t>(producer) + 1];
    }

    for (std::size_t i = 0; i < numOps; ++i)
        schedule.consumerOffsets[i + 1] += schedule.consumerOffsets[i];

    // Pass 2: fill the edge array. Consumers land in ascending op order because
    // ops are visited in order and each producer precedes all of its consumers.
    schedule.consumerEdges.assign(static_cast<std::size_t>(schedule.consumerOffsets[numOps]),
                                  INVALID_OP_ID);
    std::vector<int> cursor(schedule.consumerOffsets.begin(), schedule.consumerOffsets.end() - 1);
    for (std::size_t i = 0; i < numOps; ++i) {
        for (const auto producer : distinctProducers(plan.ops[i]))
            schedule.consumerEdges[static_cast<std::size_t>(
                cursor[static_cast<std::size_t>(producer)]++)] = static_cast<OpId>(i);
    }

    return schedule;
}

void bakeScheduling(RenderPlan& plan) {
    auto schedule = scheduleOf(plan);
    plan.dependencyCounts = std::move(schedule.dependencyCounts);
    plan.consumerOffsets = std::move(schedule.consumerOffsets);
    plan.consumerEdges = std::move(schedule.consumerEdges);
    plan.initialReadyOps = std::move(schedule.initialReadyOps);
}

bool carriesSchedule(const RenderPlan& plan) {
    const auto expected = scheduleOf(plan);
    return plan.dependencyCounts == expected.dependencyCounts &&
           plan.consumerOffsets == expected.consumerOffsets &&
           plan.consumerEdges == expected.consumerEdges &&
           plan.initialReadyOps == expected.initialReadyOps;
}

std::uint64_t planFingerprint(const RenderPlan& plan) {
    // FNV-1a. Not a cryptographic claim: it has to separate plans that differ
    // structurally, and it runs off the audio thread once per compile.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };

    mix(static_cast<std::uint64_t>(plan.version));
    mix(plan.ops.size());

    for (const auto& op : plan.ops) {
        mix(static_cast<std::uint64_t>(op.kind));
        mix(static_cast<std::uint64_t>(op.liveness));
        mix(static_cast<std::uint64_t>(op.key.trackId));
        mix(static_cast<std::uint64_t>(op.key.rackId));
        mix(static_cast<std::uint64_t>(op.key.chainId));
        mix(static_cast<std::uint64_t>(op.key.deviceId));
        mix(static_cast<std::uint64_t>(op.key.role));
        mix(static_cast<std::uint64_t>(op.key.index));

        mix(op.inputs.size());
        for (const auto& input : op.inputs) {
            mix(static_cast<std::uint64_t>(input.op));
            mix(static_cast<std::uint64_t>(input.port));
        }

        mix(op.outputs.size());
        for (const auto output : op.outputs)
            mix(static_cast<std::uint64_t>(output));
    }

    return hash;
}

std::vector<std::string> validatePlan(const RenderPlan& plan) {
    std::vector<std::string> problems;
    const auto numOps = static_cast<OpId>(plan.ops.size());

    // How many input slots anywhere in the plan read each op. Only the delay
    // rules below need it, and they need it before the op they are checking.
    std::vector<int> readerCounts(plan.ops.size(), 0);
    for (const auto& op : plan.ops)
        for (const auto& input : op.inputs)
            if (input.valid() && input.op >= 0 && input.op < numOps)
                ++readerCounts[static_cast<std::size_t>(input.op)];

    // The differ hash-joins old and new plans on OpKey, so a duplicate key does
    // not fail loudly: it carries one op's state into another. Identity bugs
    // are the concentrated risk of this design, so uniqueness is an invariant
    // here rather than a convention the compiler happens to keep.
    std::map<OpKey, OpId> keyOwners;

    for (OpId i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[static_cast<std::size_t>(i)];
        const auto label =
            "op " + std::to_string(i) + " (" + toString(op.kind) + " " + toString(op.key) + "): ";

        if (const auto [owner, inserted] = keyOwners.emplace(op.key, i); !inserted)
            problems.push_back(label + "has the same key as op " + std::to_string(owner->second) +
                               ", so the differ cannot tell them apart");

        // Output is the plan's only sink; everything else must produce.
        if (op.outputs.empty() != (op.kind == OpKind::Output))
            problems.push_back(label + (op.outputs.empty() ? "no output port"
                                                           : "is a sink and must have no ports"));

        const auto arity = arityOf(op.kind);
        if (arity >= 0 && static_cast<int>(op.inputs.size()) != arity)
            problems.push_back(label + "expected " + std::to_string(arity) + " input slots, has " +
                               std::to_string(op.inputs.size()));

        for (std::size_t slot = 0; slot < op.inputs.size(); ++slot) {
            const auto& input = op.inputs[slot];
            if (!input.valid()) {
                if (arity < 0)
                    problems.push_back(label + "variadic input " + std::to_string(slot) +
                                       " is unconnected");
                continue;
            }
            if (input.op < 0 || input.op >= i) {
                problems.push_back(label + "input " + std::to_string(slot) + " references op " +
                                   std::to_string(input.op) + ", which is not an earlier op");
                continue;
            }
            const auto& producer = plan.ops[static_cast<std::size_t>(input.op)];
            if (input.port < 0 || input.port >= static_cast<int>(producer.outputs.size())) {
                problems.push_back(label + "input " + std::to_string(slot) + " references port " +
                                   std::to_string(input.port) + " of op " +
                                   std::to_string(input.op) + ", which has " +
                                   std::to_string(producer.outputs.size()) + " ports");
                continue;
            }
            // A delay carries whatever reaches it, so its own output port is
            // what its input has to agree with.
            const auto expected =
                op.kind == OpKind::Delay
                    ? (op.outputs.empty() ? SignalKind::Audio : op.outputs.front())
                    : ((op.kind == OpKind::MergeMidi ||
                        ((op.kind == OpKind::Device || op.kind == OpKind::Fader) && slot == 1))
                           ? SignalKind::Midi
                           : SignalKind::Audio);
            const auto actual = producer.outputs[static_cast<std::size_t>(input.port)];
            if (actual != expected)
                problems.push_back(label + "input " + std::to_string(slot) + " is " +
                                   toString(actual) + ", expected " + toString(expected));
        }

        // A delay's sample count is not in the plan: it is resolved when the
        // plan is prepared, from the latency the ops upstream of its consumer
        // report. That resolution walks the plan once, forwards, and reads each
        // delay's count off the consumer it feeds, so it is only well defined
        // while these three hold. They are cheap here and impossible to check
        // anywhere else without walking the whole plan again.
        if (op.kind == OpKind::Delay) {
            if (!isInputDelayRole(op.key.role))
                problems.push_back(label + "is a delay but is not keyed to an input slot");

            if (op.inputs.empty() || !op.inputs.front().valid())
                problems.push_back(label + "is a delay with nothing to compensate");

            if (readerCounts[static_cast<std::size_t>(i)] != 1)
                problems.push_back(label + "is a delay read by " +
                                   std::to_string(readerCounts[static_cast<std::size_t>(i)]) +
                                   " input slots, and a delay compensates exactly one edge");

            if (!op.inputs.empty() && op.inputs.front().valid() && op.inputs.front().op >= 0 &&
                op.inputs.front().op < i &&
                plan.ops[static_cast<std::size_t>(op.inputs.front().op)].kind == OpKind::Delay)
                problems.push_back(label + "is a delay reading another delay, which double-counts "
                                           "the same edge's compensation");
        } else if (isInputDelayRole(op.key.role)) {
            problems.push_back(label + "carries an input-delay role but is not a delay");
        }

        // A fade is the one op a plan can hold that the compiler did not emit,
        // so the shape it is supposed to have is worth stating where every plan
        // passes rather than trusting the pass that inserts them. Both sides
        // connected because a fade from nothing is a fade in from silence, and
        // the key because the index is what keeps two fades at one location
        // apart: the differ joins on it, and a fade adopting the wrong ramp
        // resumes at a position that belongs to another edge.
        if ((op.kind == OpKind::Crossfade) != (op.key.role == OpRole::EdgeCrossfade)) {
            problems.push_back(label + (op.kind == OpKind::Crossfade
                                            ? "is a crossfade but is not keyed as one"
                                            : "carries a crossfade role but is not a crossfade"));
        } else if (op.kind == OpKind::Crossfade) {
            if (op.inputs.size() != 2 || !op.inputs[0].valid() || !op.inputs[1].valid())
                problems.push_back(label + "is a crossfade without both sides of the edge");

            if (op.outputs.size() != 1 || op.outputs.front() != SignalKind::Audio)
                problems.push_back(label + "is a crossfade and does not produce one audio port");

            if (op.key.index < 0)
                problems.push_back(label + "is a crossfade whose key index does not decode: " +
                                   std::to_string(op.key.index));
        }

        // Out-of-range inputs are already reported above; skip them here so a
        // malformed plan cannot walk off the op vector.
        const auto liveInput = std::ranges::find_if(op.inputs, [&plan, i](const PortRef& input) {
            return input.valid() && input.op >= 0 && input.op < i &&
                   plan.ops[static_cast<std::size_t>(input.op)].liveness == LivenessDomain::Live;
        });
        const auto readsLive = liveInput != op.inputs.end();

        if (op.liveness == LivenessDomain::Deterministic && readsLive)
            problems.push_back(label + "is deterministic but reads live op " +
                               std::to_string(liveInput->op));

        // The converse matters just as much and is harder to notice, because
        // over-tagging is semantically harmless: it only shrinks what the
        // anticipative executor is allowed to precompute. Liveness has to come
        // from somewhere, and only the input sources originate it.
        const auto isLiveSource = op.kind == OpKind::AudioInput || op.kind == OpKind::MidiInput;
        if (op.liveness == LivenessDomain::Live && !isLiveSource && !readsLive)
            problems.push_back(label + "is live but reads nothing live and is not an input source");
    }

    for (const auto outputOp : plan.outputOps) {
        if (outputOp < 0 || outputOp >= numOps)
            problems.push_back("output op " + std::to_string(outputOp) + " is out of range");
        else if (plan.ops[static_cast<std::size_t>(outputOp)].kind != OpKind::Output)
            problems.push_back("output op " + std::to_string(outputOp) + " is not an Output op");
    }

    return problems;
}

}  // namespace magda::engine
