#include "exec/PlanExecutor.hpp"

#include <algorithm>
#include <set>

namespace magda::engine {
namespace {

/// Bytes MidiBuffer spends on one event: a sample position, a length, and the
/// message itself. Sized for the longest short message rather than the
/// average, so a reservation computed from an event count always holds.
constexpr int kMidiBytesPerEvent =
    static_cast<int>(sizeof(std::int32_t) + sizeof(std::uint16_t)) + 3;

/// Per-channel gain for a stereo pair. Anything wider alternates, which keeps
/// the pairs correct if a device ever reports more than two channels; the model
/// has no way to express one today.
float channelGain(const OpValue& value, int channel) {
    return channel % 2 == 0 ? value.gainLeft : value.gainRight;
}

void copyWithGain(juce::dsp::AudioBlock<float> destination, juce::dsp::AudioBlock<float> source,
                  const OpValue& value, int numSamples) {
    const auto numChannels =
        static_cast<int>(std::min(destination.getNumChannels(), source.getNumChannels()));
    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::copyWithMultiply(
            destination.getChannelPointer(static_cast<std::size_t>(channel)),
            source.getChannelPointer(static_cast<std::size_t>(channel)),
            channelGain(value, channel), numSamples);
}

void applyGain(juce::dsp::AudioBlock<float> block, const OpValue& value, int numSamples) {
    for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel)
        juce::FloatVectorOperations::multiply(block.getChannelPointer(channel),
                                              channelGain(value, static_cast<int>(channel)),
                                              numSamples);
}

float peakOf(juce::dsp::AudioBlock<float> block, int numSamples) {
    float peak = 0.0f;
    for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel) {
        const auto range = juce::FloatVectorOperations::findMinAndMax(
            block.getChannelPointer(channel), numSamples);
        peak = std::max({peak, std::abs(range.getStart()), std::abs(range.getEnd())});
    }
    return peak;
}

}  // namespace

void AudioDelayLine::prepare(int numChannels, int delaySamples, int maxBlockSize) {
    delay_ = delaySamples;
    writePosition_ = 0;
    ring_.setSize(numChannels, delaySamples + maxBlockSize, false, true, false);
    ring_.clear();
}

void AudioDelayLine::process(juce::dsp::AudioBlock<float> block, int numSamples) {
    const auto capacity = ring_.getNumSamples();
    const auto numChannels =
        std::min(ring_.getNumChannels(), static_cast<int>(block.getNumChannels()));

    // Written before it is read, so a block whose delay is shorter than itself
    // reads back the samples it just handed over, and an output sharing its
    // input's buffer is no different from one that does not.
    for (int done = 0; done < numSamples;) {
        const auto chunk = std::min(numSamples - done, capacity - writePosition_);
        for (int channel = 0; channel < numChannels; ++channel)
            ring_.copyFrom(channel, writePosition_,
                           block.getChannelPointer(static_cast<std::size_t>(channel)) + done,
                           chunk);
        writePosition_ = (writePosition_ + chunk) % capacity;
        done += chunk;
    }

    auto readPosition = writePosition_ - numSamples - delay_;
    while (readPosition < 0)
        readPosition += capacity;

    for (int done = 0; done < numSamples;) {
        const auto chunk = std::min(numSamples - done, capacity - readPosition);
        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::copy(
                block.getChannelPointer(static_cast<std::size_t>(channel)) + done,
                ring_.getReadPointer(channel, readPosition), chunk);
        readPosition = (readPosition + chunk) % capacity;
        done += chunk;
    }
}

void MidiDelayLine::prepare(int delaySamples, int capacityBytes) {
    delay_ = delaySamples;
    pending_.clear();
    scratch_.clear();
    // Both, and to the same size: they are swapped every block, so the storage
    // travels with them and one of them being the small one would allocate on
    // the callback the first time it came round.
    pending_.ensureSize(static_cast<std::size_t>(capacityBytes));
    scratch_.ensureSize(static_cast<std::size_t>(capacityBytes));
}

void MidiDelayLine::process(const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples) {
    scratch_.clear();

    // Positions are relative to the start of the block, so what is still in the
    // future stays here and moves one block closer.
    for (const auto metadata : pending_) {
        if (metadata.samplePosition < numSamples)
            out.addEvent(metadata.data, metadata.numBytes, metadata.samplePosition);
        else
            scratch_.addEvent(metadata.data, metadata.numBytes,
                              metadata.samplePosition - numSamples);
    }

    for (const auto metadata : in) {
        const auto position = metadata.samplePosition + delay_;
        if (position < numSamples)
            out.addEvent(metadata.data, metadata.numBytes, position);
        else
            scratch_.addEvent(metadata.data, metadata.numBytes, position - numSamples);
    }

    pending_.swapWith(scratch_);
}

void PlanExecutor::reset() {
    plan_ = nullptr;
    planFingerprint_ = 0;
    latencySamples_ = 0;
    audioSlots_.clear();
    midiSlots_.clear();
    midiByteBounds_.clear();
    portOffsets_.clear();
    portSlots_.clear();
    writesInPlace_.clear();
    audioDelayForOp_.clear();
    midiDelayForOp_.clear();
    audioDelays_.clear();
    midiDelays_.clear();
    deviceForOp_.clear();
    audioSourceForOp_.clear();
    midiSourceForOp_.clear();
    meterLevels_.clear();
}

std::vector<std::string> PlanExecutor::prepare(const RenderPlan& plan, const PlanBindings& bindings,
                                               const RenderContext& context) {
    reset();

    std::vector<std::string> messages;

    // A malformed plan is refused rather than half-executed: its ports and
    // ordering are what makes the straight walk below safe.
    auto problems = validatePlan(plan);
    if (!problems.empty()) {
        for (auto& problem : problems)
            messages.push_back("plan is not well formed: " + std::move(problem));
        return messages;
    }

    context_ = context;
    const auto numOps = plan.ops.size();

    deviceForOp_.assign(numOps, nullptr);
    audioSourceForOp_.assign(numOps, nullptr);
    midiSourceForOp_.assign(numOps, nullptr);
    meterLevels_.assign(numOps, 0.0f);

    const auto describe = [&plan](std::size_t index) {
        return "op " + std::to_string(index) + " (" + toString(plan.ops[index].kind) + " " +
               toString(plan.ops[index].key) + "): ";
    };

    const auto findAudioSource = [](const auto& map, TrackId trackId) -> EngineAudioSource* {
        const auto found = map.find(trackId);
        return found == map.end() ? nullptr : found->second;
    };
    const auto findMidiSource = [](const auto& map, TrackId trackId) -> EngineMidiSource* {
        const auto found = map.find(trackId);
        return found == map.end() ? nullptr : found->second;
    };

    for (std::size_t i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[i];
        const auto trackId = op.key.trackId;

        switch (op.kind) {
            case OpKind::ClipAudio:
                audioSourceForOp_[i] = findAudioSource(bindings.clipAudio, trackId);
                if (audioSourceForOp_[i] == nullptr)
                    messages.push_back(describe(i) + "no clip audio source bound for track " +
                                       std::to_string(trackId) + ", it renders silence");
                break;

            case OpKind::AudioInput:
                audioSourceForOp_[i] = findAudioSource(bindings.audioInputs, trackId);
                if (audioSourceForOp_[i] == nullptr)
                    messages.push_back(describe(i) + "no live audio input bound for track " +
                                       std::to_string(trackId) + ", it renders silence");
                break;

            case OpKind::ClipMidi:
                midiSourceForOp_[i] = findMidiSource(bindings.clipMidi, trackId);
                if (midiSourceForOp_[i] == nullptr)
                    messages.push_back(describe(i) + "no clip MIDI source bound for track " +
                                       std::to_string(trackId) + ", it renders nothing");
                break;

            case OpKind::MidiInput:
                midiSourceForOp_[i] = findMidiSource(bindings.midiInputs, trackId);
                if (midiSourceForOp_[i] == nullptr)
                    messages.push_back(describe(i) + "no live MIDI input bound for track " +
                                       std::to_string(trackId) + ", it renders nothing");
                break;

            case OpKind::Device: {
                const auto found = bindings.devices.find(op.key.deviceId);
                if (found == bindings.devices.end() || found->second == nullptr) {
                    // Passing audio through is what the current engine does
                    // with a plugin that failed to load, and it keeps the rest
                    // of the chain testable instead of silencing the track.
                    messages.push_back(describe(i) + "no device bound for device " +
                                       std::to_string(op.key.deviceId) +
                                       ", it passes audio through");
                    break;
                }
                deviceForOp_[i] = found->second;
                break;
            }

            default:
                break;
        }
    }

    // Prepare each bound object once, however many ops reference it.
    std::set<const void*> prepared;
    const auto prepareOnce = [&prepared, &context](auto* object) {
        if (object != nullptr && prepared.insert(object).second)
            object->prepare(context);
    };

    for (std::size_t i = 0; i < numOps; ++i) {
        prepareOnce(audioSourceForOp_[i]);
        prepareOnce(midiSourceForOp_[i]);
        prepareOnce(deviceForOp_[i]);
    }

    // --- what the bindings decide -------------------------------------------
    //
    // Everything below reads the instances rather than the plan or the model. A
    // plugin only reports its latency once it is loaded and prepared, which is
    // now, and how many samples each delay holds decides which ports can share
    // a buffer. So the two passes run here, in this order, on every prepare.

    std::vector<int> deviceLatency(numOps, 0);
    for (std::size_t i = 0; i < numOps; ++i)
        if (const auto* device = deviceForOp_[i]; device != nullptr)
            deviceLatency[i] = device->latencySamples();

    portOffsets_ = portOffsetsOf(plan);
    const auto latency = resolvePlanLatency(plan, portOffsets_, deviceLatency);
    latencySamples_ = latency.outputLatency;

    const auto layout = assignBuffers(plan, portOffsets_, latency.delaySamples);
    portSlots_ = layout.portSlots;
    writesInPlace_ = layout.writesInPlace;

    audioSlots_.resize(static_cast<std::size_t>(layout.numAudioSlots));
    for (auto& buffer : audioSlots_)
        buffer.setSize(context_.numChannels, context_.maxBlockSize, false, true, false);

    // Reserving a flat amount per MIDI port is not enough: a merge carries
    // everything that reaches it, so fan-in outgrows any fixed figure and the
    // first block that does grows a buffer on the callback. The bound is
    // computed through the MIDI graph instead, which ops being in dependency
    // order makes a single forward pass. Producers are capped by contract
    // (kMaxMidiBytesPerPort), and that cap is what the sums are built from.
    //
    // Ports sharing a buffer changes what that bound is for: one port at a time
    // is live in a slot, so a slot has to hold the largest of its users rather
    // than the sum of them.
    const auto numPorts = static_cast<std::size_t>(portOffsets_.back());
    std::vector<int> portMidiBytes(numPorts, kMaxMidiBytesPerPort);
    const auto flatPort = [this](const PortRef& ref) {
        return static_cast<std::size_t>(portOffsets_[static_cast<std::size_t>(ref.op)] + ref.port);
    };

    for (std::size_t i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[i];

        if (op.kind == OpKind::Delay) {
            if (op.outputs.front() != SignalKind::Midi)
                continue;
            // A delay's output block covers one block of its input's time, and
            // a block-length window falls across two of them unless the delay
            // is a whole number of blocks. Zero is not a delay at all: the op
            // does not run and the port is its input's.
            const auto carried = portMidiBytes[flatPort(op.inputs.front())];
            portMidiBytes[static_cast<std::size_t>(portOffsets_[i])] =
                latency.delaySamples[i] == 0 ? carried : 2 * carried;
            continue;
        }

        if (op.kind != OpKind::MergeMidi && op.kind != OpKind::Fader)
            continue;

        int carried = 0;
        for (const auto& input : op.inputs) {
            if (!input.valid())
                continue;
            if (plan.ops[static_cast<std::size_t>(input.op)]
                    .outputs[static_cast<std::size_t>(input.port)] != SignalKind::Midi)
                continue;
            carried += portMidiBytes[flatPort(input)];
        }

        for (std::size_t port = 0; port < op.outputs.size(); ++port)
            if (op.outputs[port] == SignalKind::Midi)
                portMidiBytes[static_cast<std::size_t>(portOffsets_[i]) + port] = carried;
    }

    midiSlots_.resize(static_cast<std::size_t>(layout.numMidiSlots));
    midiByteBounds_.assign(static_cast<std::size_t>(layout.numMidiSlots), 0);
    for (std::size_t i = 0; i < numOps; ++i) {
        for (std::size_t port = 0; port < plan.ops[i].outputs.size(); ++port) {
            if (plan.ops[i].outputs[port] != SignalKind::Midi)
                continue;
            const auto flat = static_cast<std::size_t>(portOffsets_[i]) + port;
            auto& bound = midiByteBounds_[static_cast<std::size_t>(portSlots_[flat])];
            bound = std::max(bound, portMidiBytes[flat]);
        }
    }

    for (std::size_t slot = 0; slot < midiSlots_.size(); ++slot)
        midiSlots_[slot].ensureSize(static_cast<std::size_t>(midiByteBounds_[slot]));

    // Delay lines, for the edges that turned out to need one. A plan with no
    // latency in it allocates nothing here, which is the whole point of
    // resolving the counts before anything is sized.
    audioDelayForOp_.assign(numOps, -1);
    midiDelayForOp_.assign(numOps, -1);
    int numAudioDelays = 0;
    int numMidiDelays = 0;
    for (std::size_t i = 0; i < numOps; ++i) {
        if (plan.ops[i].kind != OpKind::Delay || latency.delaySamples[i] <= 0)
            continue;
        if (plan.ops[i].outputs.front() == SignalKind::Audio)
            audioDelayForOp_[i] = numAudioDelays++;
        else
            midiDelayForOp_[i] = numMidiDelays++;
    }

    audioDelays_.resize(static_cast<std::size_t>(numAudioDelays));
    midiDelays_.resize(static_cast<std::size_t>(numMidiDelays));
    for (std::size_t i = 0; i < numOps; ++i) {
        const auto samples = latency.delaySamples[i];
        if (audioDelayForOp_[i] >= 0)
            audioDelays_[static_cast<std::size_t>(audioDelayForOp_[i])].prepare(
                context_.numChannels, samples, context_.maxBlockSize);

        if (midiDelayForOp_[i] >= 0) {
            // Every block the delay spans is holding events, plus the one being
            // filled and the one being drained.
            const auto blocks = samples / context_.maxBlockSize + 2;
            midiDelays_[static_cast<std::size_t>(midiDelayForOp_[i])].prepare(
                samples, blocks * portMidiBytes[flatPort(plan.ops[i].inputs.front())]);
        }
    }

    silence_.setSize(context_.numChannels, context_.maxBlockSize, false, true, false);
    silence_.clear();
    noMidi_.clear();

    planFingerprint_ = planFingerprint(plan);
    plan_ = &plan;
    return messages;
}

juce::dsp::AudioBlock<float> PlanExecutor::audioIn(const PortRef& ref, int numSamples) {
    auto& buffer = ref.valid() ? audioSlots_[static_cast<std::size_t>(slotFor(ref))] : silence_;
    return juce::dsp::AudioBlock<float>(buffer).getSubBlock(0,
                                                            static_cast<std::size_t>(numSamples));
}

juce::dsp::AudioBlock<float> PlanExecutor::audioOut(OpId op, int port, int numSamples) {
    auto& buffer = audioSlots_[static_cast<std::size_t>(slotFor(PortRef{op, port}))];
    return juce::dsp::AudioBlock<float>(buffer).getSubBlock(0,
                                                            static_cast<std::size_t>(numSamples));
}

const juce::MidiBuffer& PlanExecutor::midiIn(const PortRef& ref) const {
    return ref.valid() ? midiSlots_[static_cast<std::size_t>(slotFor(ref))] : noMidi_;
}

juce::MidiBuffer& PlanExecutor::midiOut(OpId op, int port) {
    return midiSlots_[static_cast<std::size_t>(slotFor(PortRef{op, port}))];
}

void PlanExecutor::process(const PlanValues& values, const BlockInfo& requestedBlock,
                           juce::AudioBuffer<float>& output) {
    output.clear();
    if (plan_ == nullptr)
        return;

    // A host asking for more than the block the plan was prepared for would
    // otherwise leave the tail of its buffer silent with nothing saying so.
    jassert(requestedBlock.numSamples <= context_.maxBlockSize);

    auto block = requestedBlock;
    block.numSamples = std::min(block.numSamples, context_.maxBlockSize);
    const auto numSamples = block.numSamples;
    if (numSamples <= 0)
        return;

    // Values are resolved against a plan and published separately from it, so
    // a table can outlive the plan it was made for. Matching op counts prove
    // nothing: a structural edit can replace ops and keep the count, and the
    // stale gains and mutes would land on whatever op now holds each index.
    // The fingerprint is what says the two belong together; without it, unity
    // is the safe reading.
    static constexpr OpValue kUnity;
    const auto haveValues =
        values.planFingerprint == planFingerprint_ && values.ops.size() == plan_->ops.size();

    for (std::size_t i = 0; i < plan_->ops.size(); ++i) {
        const auto& op = plan_->ops[i];
        const auto& value = haveValues ? values.ops[i] : kUnity;
        const auto id = static_cast<OpId>(i);

        switch (op.kind) {
            case OpKind::ClipAudio:
            case OpKind::AudioInput: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent || audioSourceForOp_[i] == nullptr)
                    out.clear();
                else
                    audioSourceForOp_[i]->render(block, out);
                break;
            }

            case OpKind::ClipMidi:
            case OpKind::MidiInput: {
                auto& out = midiOut(id, 0);
                out.clear();
                if (!value.silent && midiSourceForOp_[i] != nullptr) {
                    midiSourceForOp_[i]->render(block, out);
                    // Bytes, not events: one SysEx dump outweighs a thousand
                    // notes, and an event count would wave it through.
                    jassert(out.data.size() <= kMaxMidiBytesPerPort);
                }
                break;
            }

            case OpKind::MixAudio: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent) {
                    out.clear();
                    break;
                }
                // Summed in compiled order, never in the order inputs finish:
                // float addition is not associative, so this is what makes the
                // parallel executor's output bit-identical to this one.
                //
                // The first input may already be here, in the buffer it was
                // written into. Skipping the copy is the same sum, not a
                // different one: it is added first either way.
                auto pending = writesInPlace(i);
                if (!pending)
                    out.clear();
                for (const auto& input : op.inputs) {
                    if (!input.valid())
                        continue;
                    if (pending) {
                        pending = false;
                        continue;
                    }
                    out.add(audioIn(input, numSamples));
                }
                break;
            }

            case OpKind::Delay: {
                // A delay runs whatever the value layer says about the ops
                // around it. One that stopped writing while its chain was
                // silent would hand back audio from before the silence when
                // the chain returned, which is the one thing a delay line
                // cannot do.
                if (op.outputs.front() == SignalKind::Audio) {
                    const auto line = audioDelayForOp_[i];
                    if (line < 0)
                        break;  // no samples to hold: its port is its input's
                    auto out = audioOut(id, 0, numSamples);
                    if (!writesInPlace(i))
                        out.copyFrom(audioIn(op.inputs[0], numSamples));
                    audioDelays_[static_cast<std::size_t>(line)].process(out, numSamples);
                    break;
                }

                const auto line = midiDelayForOp_[i];
                if (line < 0)
                    break;
                auto& out = midiOut(id, 0);
                out.clear();
                midiDelays_[static_cast<std::size_t>(line)].process(midiIn(op.inputs[0]), out,
                                                                    numSamples);
                jassert(out.data.size() <=
                        static_cast<std::size_t>(
                            midiByteBounds_[static_cast<std::size_t>(slotFor(PortRef{id, 0}))]));
                break;
            }

            case OpKind::MergeMidi: {
                auto& out = midiOut(id, 0);
                out.clear();
                if (value.silent)
                    break;
                for (const auto& input : op.inputs)
                    if (input.valid())
                        out.addEvents(midiIn(input), 0, numSamples, 0);
                break;
            }

            case OpKind::Device: {
                auto audio = audioOut(id, 0, numSamples);
                const auto producesMidi =
                    op.outputs.size() > 1 && op.outputs[1] == SignalKind::Midi;
                juce::MidiBuffer* deviceMidiOut = nullptr;
                if (producesMidi) {
                    deviceMidiOut = &midiOut(id, 1);
                    deviceMidiOut->clear();
                }

                if (value.silent) {
                    audio.clear();
                    break;
                }

                if (!op.inputs[0].valid())
                    audio.clear();
                else if (!writesInPlace(i))
                    audio.copyFrom(audioIn(op.inputs[0], numSamples));

                auto* device = deviceForOp_[i];
                if (device == nullptr)
                    break;

                DeviceBlock deviceBlock{audio, &midiIn(op.inputs[1]), deviceMidiOut, {}, block};
                if (op.inputs[2].valid())
                    deviceBlock.sidechain = audioIn(op.inputs[2], numSamples);
                device->process(deviceBlock);
                jassert(deviceMidiOut == nullptr ||
                        deviceMidiOut->data.size() <= kMaxMidiBytesPerPort);
                break;
            }

            case OpKind::Gain:
            case OpKind::SendTap: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent || !op.inputs[0].valid())
                    out.clear();
                else if (writesInPlace(i))
                    applyGain(out, value, numSamples);
                else
                    copyWithGain(out, audioIn(op.inputs[0], numSamples), value, numSamples);
                break;
            }

            case OpKind::Fader: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent || !op.inputs[0].valid())
                    out.clear();
                else if (writesInPlace(i))
                    applyGain(out, value, numSamples);
                else
                    copyWithGain(out, audioIn(op.inputs[0], numSamples), value, numSamples);

                // A rack chain's MIDI leaves through its fader too, so that one
                // silent flag takes the whole chain out of the mix. Gain does
                // not apply to it: there is no such thing as MIDI at half
                // volume, only MIDI that is connected or is not.
                if (op.outputs.size() > 1 && op.outputs[1] == SignalKind::Midi) {
                    auto& outMidiBuffer = midiOut(id, 1);
                    outMidiBuffer.clear();
                    if (!value.silent && op.inputs[1].valid())
                        outMidiBuffer.addEvents(midiIn(op.inputs[1]), 0, numSamples, 0);
                }
                break;
            }

            case OpKind::Meter: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent || !op.inputs[0].valid())
                    out.clear();
                else if (!writesInPlace(i))
                    out.copyFrom(audioIn(op.inputs[0], numSamples));
                meterLevels_[i] = peakOf(out, numSamples);
                break;
            }

            case OpKind::Output: {
                if (value.silent || !op.inputs[0].valid())
                    break;
                auto in = audioIn(op.inputs[0], numSamples);
                const auto numChannels =
                    std::min(static_cast<int>(in.getNumChannels()), output.getNumChannels());
                for (int channel = 0; channel < numChannels; ++channel)
                    output.addFrom(channel, 0,
                                   in.getChannelPointer(static_cast<std::size_t>(channel)),
                                   numSamples);
                break;
            }
        }
    }
}

}  // namespace magda::engine
