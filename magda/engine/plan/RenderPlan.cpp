#include "plan/RenderPlan.hpp"

#include <algorithm>
#include <map>
#include <ostream>
#include <set>
#include <tuple>

namespace magda::engine {

bool OpKey::operator<(const OpKey& o) const {
    return std::tie(trackId, rackId, chainId, segment, deviceId, role, index) <
           std::tie(o.trackId, o.rackId, o.chainId, o.segment, o.deviceId, o.role, o.index);
}

int arityOf(OpKind kind) {
    switch (kind) {
        case OpKind::ClipAudio:
        case OpKind::ClipMidi:
        case OpKind::AudioInput:
        case OpKind::MidiInput:
        case OpKind::SessionAudio:
        case OpKind::SessionMidi:
            return 0;
        case OpKind::Device:
            return 3;  // audio, MIDI, sidechain audio
        case OpKind::Fader:
            return 2;  // audio, MIDI
        case OpKind::Crossfade:
            return 2;  // the edge as it was, the edge as it is
        case OpKind::Subtract:
            return 2;  // the processed signal, the dry signal it is measured against
        case OpKind::MixAudio:
        case OpKind::MergeMidi:
            return -1;  // variadic
        case OpKind::Delay:
        case OpKind::Gain:
        case OpKind::SendTap:
        case OpKind::Meter:
        case OpKind::Output:
        case OpKind::MidiNoteGate:
            return 1;
        case OpKind::ModSource:
            return 2;  // the source's audio at this tap's point, the source's MIDI
        case OpKind::InsertSend:
            return 2;  // what leaves the machine: audio, MIDI
        case OpKind::InsertReturn:
            return 0;  // what comes back is a source, like a live input
        case OpKind::FeedbackSend:
            // The signal the carry holds, and the return that reads it: the
            // second is ordering only, so every schedule reads last block's
            // carry before this block overwrites it.
            return 2;
        case OpKind::FeedbackReturn:
            return 0;  // last block's carry, which nothing in this block produced
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
        case OpKind::SessionAudio:
            return "SessionAudio";
        case OpKind::SessionMidi:
            return "SessionMidi";
        case OpKind::Device:
            return "Device";
        case OpKind::MixAudio:
            return "MixAudio";
        case OpKind::MergeMidi:
            return "MergeMidi";
        case OpKind::MidiNoteGate:
            return "MidiNoteGate";
        case OpKind::Subtract:
            return "Subtract";
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
        case OpKind::ModSource:
            return "ModSource";
        case OpKind::Output:
            return "Output";
        case OpKind::InsertSend:
            return "InsertSend";
        case OpKind::InsertReturn:
            return "InsertReturn";
        case OpKind::FeedbackSend:
            return "FeedbackSend";
        case OpKind::FeedbackReturn:
            return "FeedbackReturn";
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
        case OpRole::LiveInputMeter:
            return "liveInputMeter";
        case OpRole::LiveMidiInput:
            return "liveMidiInput";
        case OpRole::LiveInputGate:
            return "liveInputGate";
        case OpRole::SessionAudio:
            return "sessionAudio";
        case OpRole::SessionMidi:
            return "sessionMidi";
        case OpRole::TrackAudioInput:
            return "trackAudioInput";
        case OpRole::TrackMidiInput:
            return "trackMidiInput";
        case OpRole::DeviceProcess:
            return "deviceProcess";
        case OpRole::DeviceInject:
            return "deviceInject";
        case OpRole::DeviceDelta:
            return "deviceDelta";
        case OpRole::DeviceGain:
            return "deviceGain";
        case OpRole::DeviceSidechainGain:
            return "deviceSidechainGain";
        case OpRole::DeviceMeter:
            return "deviceMeter";
        case OpRole::ChainMidiMerge:
            return "chainMidiMerge";
        case OpRole::PadNoteGate:
            return "padNoteGate";
        case OpRole::RackChainFader:
            return "rackChainFader";
        case OpRole::RackMix:
            return "rackMix";
        case OpRole::RackMidiMix:
            return "rackMidiMix";
        case OpRole::RackFader:
            return "rackFader";
        case OpRole::RackDelta:
            return "rackDelta";
        case OpRole::RackMeter:
            return "rackMeter";
        case OpRole::TrackFader:
            return "trackFader";
        case OpRole::TrackMeter:
            return "trackMeter";
        case OpRole::TrackMute:
            return "trackMute";
        case OpRole::SendTap:
            return "sendTap";
        case OpRole::ModulationTap:
            return "modulationTap";
        case OpRole::InsertSend:
            return "insertSend";
        case OpRole::InsertReturn:
            return "insertReturn";
        case OpRole::FeedbackSend:
            return "feedbackSend";
        case OpRole::FeedbackReturn:
            return "feedbackReturn";
        case OpRole::InputRouteGate:
            return "inputRouteGate";
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
        case OpRole::SubtractInputDelay:
            return "subtractInputDelay";
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
    if (key.deviceId != INVALID_DEVICE_ID) {
        if (key.segment == ChainSegment::PostFx)
            out += "/PF";
        else if (key.segment == ChainSegment::MixerAnalysis)
            out += "/MA";
        out += "/D" + std::to_string(key.deviceId);
    }
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

std::string toString(const DeviceKey& key) {
    std::string out;
    if (key.segment == ChainSegment::PostFx)
        out = "PostFx/";
    else if (key.segment == ChainSegment::MixerAnalysis)
        out = "MixerAnalysis/";
    out += "D" + std::to_string(key.deviceId);
    return out;
}

std::ostream& operator<<(std::ostream& out, const DeviceKey& key) {
    return out << toString(key);
}

namespace {

/// Whether a role names a delay on one input slot of the op it is keyed to.
bool isInputDelayRole(OpRole role) {
    switch (role) {
        case OpRole::MixInputDelay:
        case OpRole::MergeInputDelay:
        case OpRole::DeviceInputDelay:
        case OpRole::FaderInputDelay:
        case OpRole::SubtractInputDelay:
            return true;
        default:
            return false;
    }
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
        mix(static_cast<std::uint64_t>(op.key.segment));
        mix(static_cast<std::uint64_t>(op.key.deviceId));
        mix(static_cast<std::uint64_t>(op.key.role));
        mix(static_cast<std::uint64_t>(op.key.index));

        mix(op.inputs.size());
        for (const auto& input : op.inputs) {
            mix(static_cast<std::uint64_t>(input.op));
            mix(static_cast<std::uint64_t>(input.port));
        }

        mix(op.outputs.size());
        for (const auto output : op.outputs) {
            mix(static_cast<std::uint64_t>(output.kind));
            mix(static_cast<std::uint64_t>(output.channels));
        }
        mix(static_cast<std::uint64_t>(op.audioInputChannels));
        mix(static_cast<std::uint64_t>(op.hardwareOutput.leftChannel));
        mix(static_cast<std::uint64_t>(op.hardwareOutput.rightChannel));
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

    // Which carries hold a live signal, by the key of the return that reads
    // them. Collected up front because a send sits after the return it fills,
    // so the liveness check below has nothing to look at by the time it runs.
    std::set<OpKey> liveCarries;
    for (const auto& op : plan.ops) {
        if (op.kind != OpKind::FeedbackSend || op.inputs.empty() || !op.inputs.front().valid())
            continue;

        const auto filled = op.inputs.front().op;
        if (filled < 0 || filled >= numOps ||
            plan.ops[static_cast<std::size_t>(filled)].liveness != LivenessDomain::Live)
            continue;

        auto key = op.key;
        key.role = OpRole::FeedbackReturn;
        liveCarries.insert(key);
    }

    for (OpId i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[static_cast<std::size_t>(i)];
        const auto label =
            "op " + std::to_string(i) + " (" + toString(op.kind) + " " + toString(op.key) + "): ";

        if (const auto [owner, inserted] = keyOwners.emplace(op.key, i); !inserted)
            problems.push_back(label + "has the same key as op " + std::to_string(owner->second) +
                               ", so the differ cannot tell them apart");

        // Two sinks and no others: the hardware output, and the tap that hands
        // a track's signal to the modulation system. Everything else produces.
        // An insert's send is a sink for the same reason the hardware output is:
        // what it writes leaves the machine, so nothing downstream reads it and
        // an op with no outputs is exactly what it is.
        // A feedback send is a sink on the same terms: what it writes is read a
        // block later, so nothing downstream of it reads it in this one.
        const bool sink = op.kind == OpKind::Output || op.kind == OpKind::ModSource ||
                          op.kind == OpKind::InsertSend || op.kind == OpKind::FeedbackSend;
        if (op.outputs.empty() != sink)
            problems.push_back(label + (op.outputs.empty() ? "no output port"
                                                           : "is a sink and must have no ports"));

        if (op.kind == OpKind::Output && !op.hardwareOutput.valid())
            problems.push_back(label + "has invalid hardware output channels");

        // A carry is audio, and the executor reads its ports as audio without
        // asking. A MIDI one would index the audio arena with a MIDI slot, so
        // the shape is enforced rather than assumed.
        if (op.kind == OpKind::FeedbackReturn &&
            (op.outputs.size() != 1 || op.outputs.front().kind != SignalKind::Audio))
            problems.push_back(label + "a feedback return carries audio on one port");

        if (op.kind == OpKind::FeedbackSend) {
            if (op.key.role != OpRole::FeedbackSend)
                problems.push_back(label + "a feedback send must carry the feedback send role");

            // The second input pairs the send with its own return, which is
            // what orders the read before the write. Bounds are checked here
            // rather than left to the generic pass below, which runs after
            // this and would be too late to stop the dereference.
            const auto paired = op.inputs.size() > 1 ? op.inputs[1] : PortRef{};
            if (paired.valid() && paired.op >= 0 && paired.op < i) {
                auto expected = op.key;
                expected.role = OpRole::FeedbackReturn;

                const auto& returned = plan.ops[static_cast<std::size_t>(paired.op)];
                if (returned.kind != OpKind::FeedbackReturn || !(returned.key == expected))
                    problems.push_back(label +
                                       "input 1 is not the feedback return this send pairs with");
            } else if (!paired.valid()) {
                problems.push_back(label + "a feedback send must name the return it pairs with");
            }
        }

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
            const bool midiSlot = op.kind == OpKind::MergeMidi || op.kind == OpKind::MidiNoteGate ||
                                  ((op.kind == OpKind::Device || op.kind == OpKind::Fader ||
                                    op.kind == OpKind::ModSource) &&
                                   slot == 1);
            const auto expected =
                op.kind == OpKind::Delay
                    ? (op.outputs.empty() ? SignalKind::Audio : op.outputs.front().kind)
                    : (midiSlot ? SignalKind::Midi : SignalKind::Audio);
            const auto actual = producer.outputs[static_cast<std::size_t>(input.port)];
            if (actual.kind != expected)
                problems.push_back(label + "input " + std::to_string(slot) + " is " +
                                   toString(actual.kind) + ", expected " + toString(expected));
        }

        // Widths describe a device and nothing else: what a plugin was asked
        // to read, and what it says it wrote. Every other port is the bus,
        // because the executor copies a narrow device port across its slot
        // before anything reads it. A MIDI port has no channels to declare.
        for (const auto output : op.outputs) {
            if (output.kind == SignalKind::Midi) {
                if (output.channels != 0)
                    problems.push_back(label + "declares midi at " +
                                       std::to_string(output.channels) + " channels");
            } else if (output.channels > 2 || (output.channels != 2 && op.kind != OpKind::Device)) {
                problems.push_back(label + "declares audio at " + std::to_string(output.channels) +
                                   " channels");
            }
        }

        if (op.audioInputChannels > 2 || (op.audioInputChannels != 0 && op.kind != OpKind::Device))
            problems.push_back(label + "reads its audio input at " +
                               std::to_string(op.audioInputChannels) + " channels");

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

            if (op.outputs.size() != 1 || op.outputs.front().kind != SignalKind::Audio)
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

        // The same rule for the one op that reads nothing and still carries
        // something: a return whose send fills it with live audio is live, and
        // saying otherwise would let every deterministic op downstream of it
        // claim it may be rendered ahead of a hardware input.
        if (op.kind == OpKind::FeedbackReturn &&
            liveCarries.contains(op.key) != (op.liveness == LivenessDomain::Live))
            problems.push_back(label +
                               (op.liveness == LivenessDomain::Live
                                    ? "is live but its send fills it with deterministic audio"
                                    : "is deterministic but its send fills it with live audio"));

        // The converse matters just as much and is harder to notice, because
        // over-tagging is semantically harmless: it only shrinks what the
        // anticipative executor is allowed to precompute. Liveness has to come
        // from somewhere, and only the input sources originate it.
        // A carry originates liveness the same way, one block removed: the
        // return reads nothing, so what justifies it is the send that fills it,
        // which is emitted later and cannot be reached by the walk above.
        const auto carriesLive = op.kind == OpKind::FeedbackReturn && liveCarries.contains(op.key);

        const auto isLiveSource = op.kind == OpKind::AudioInput || op.kind == OpKind::MidiInput;
        if (op.liveness == LivenessDomain::Live && !isLiveSource && !readsLive && !carriesLive)
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
