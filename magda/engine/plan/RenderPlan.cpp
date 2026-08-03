#include "plan/RenderPlan.hpp"

#include <algorithm>
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
        case OpKind::MixAudio:
        case OpKind::MergeMidi:
            return -1;  // variadic
        case OpKind::Gain:
        case OpKind::Fader:
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
        case OpRole::SendTap:
            return "sendTap";
        case OpRole::HardwareOutput:
            return "hardwareOutput";
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
    if (key.index != 0)
        out += "#" + std::to_string(key.index);
    return out;
}

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

}  // namespace

void bakeScheduling(RenderPlan& plan) {
    const auto numOps = plan.ops.size();

    plan.dependencyCounts.assign(numOps, 0);
    plan.initialReadyOps.clear();
    plan.consumerOffsets.assign(numOps + 1, 0);
    plan.consumerEdges.clear();

    // Pass 1: dependency counts, and per-producer consumer counts into the
    // offset array (shifted by one so the prefix sum below lands in place).
    for (std::size_t i = 0; i < numOps; ++i) {
        const auto producers = distinctProducers(plan.ops[i]);
        plan.dependencyCounts[i] = static_cast<std::uint16_t>(producers.size());
        if (producers.empty())
            plan.initialReadyOps.push_back(static_cast<OpId>(i));
        for (const auto producer : producers)
            ++plan.consumerOffsets[static_cast<std::size_t>(producer) + 1];
    }

    for (std::size_t i = 0; i < numOps; ++i)
        plan.consumerOffsets[i + 1] += plan.consumerOffsets[i];

    // Pass 2: fill the edge array. Consumers land in ascending op order because
    // ops are visited in order and each producer precedes all of its consumers.
    plan.consumerEdges.assign(static_cast<std::size_t>(plan.consumerOffsets[numOps]),
                              INVALID_OP_ID);
    std::vector<int> cursor(plan.consumerOffsets.begin(), plan.consumerOffsets.end() - 1);
    for (std::size_t i = 0; i < numOps; ++i) {
        for (const auto producer : distinctProducers(plan.ops[i]))
            plan.consumerEdges[static_cast<std::size_t>(
                cursor[static_cast<std::size_t>(producer)]++)] = static_cast<OpId>(i);
    }
}

std::vector<std::string> validatePlan(const RenderPlan& plan) {
    std::vector<std::string> problems;
    const auto numOps = static_cast<OpId>(plan.ops.size());

    for (OpId i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[static_cast<std::size_t>(i)];
        const auto label =
            "op " + std::to_string(i) + " (" + toString(op.kind) + " " + toString(op.key) + "): ";

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
            const auto expected =
                (op.kind == OpKind::MergeMidi || (op.kind == OpKind::Device && slot == 1))
                    ? SignalKind::Midi
                    : SignalKind::Audio;
            const auto actual = producer.outputs[static_cast<std::size_t>(input.port)];
            if (actual != expected)
                problems.push_back(label + "input " + std::to_string(slot) + " is " +
                                   toString(actual) + ", expected " + toString(expected));
        }

        if (op.liveness == LivenessDomain::Deterministic) {
            // Out-of-range inputs are already reported above; skip them here so
            // a malformed plan cannot walk off the op vector.
            const auto liveInput =
                std::ranges::find_if(op.inputs, [&plan, i](const PortRef& input) {
                    return input.valid() && input.op >= 0 && input.op < i &&
                           plan.ops[static_cast<std::size_t>(input.op)].liveness ==
                               LivenessDomain::Live;
                });
            if (liveInput != op.inputs.end())
                problems.push_back(label + "is deterministic but reads live op " +
                                   std::to_string(liveInput->op));
        }
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
