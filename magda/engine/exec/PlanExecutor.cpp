#include "exec/PlanExecutor.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace magda::engine {
namespace {

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
    capacity_ = capacityBytes;
    overflowed_.store(false, std::memory_order_relaxed);
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

    // The reservation covers what a port may carry over one block's worth of
    // samples, times the spans the delay holds in flight. Past it the buffer
    // grows on the callback, which is the one thing this whole arrangement
    // exists to avoid, and a producer writing faster than its budget is the
    // only way to get here. Recorded as well as asserted: a release build
    // would otherwise take the allocation and say nothing.
    if (static_cast<int>(pending_.data.size()) > capacity_) {
        overflowed_.store(true, std::memory_order_relaxed);
        jassertfalse;
    }
}

void CrossfadeRamp::prepare(int lengthSamples) {
    length_ = lengthSamples;
    remaining_.store(lengthSamples, std::memory_order_relaxed);
}

void CrossfadeRamp::process(juce::dsp::AudioBlock<float> dest, juce::dsp::AudioBlock<float> oldSide,
                            juce::dsp::AudioBlock<float> newSide, int numSamples) {
    const auto left = remaining_.load(std::memory_order_relaxed);
    const auto fading = std::max(0, std::min(left, numSamples));
    const auto position = length_ - left;

    const auto numChannels = static_cast<int>(std::min(
        dest.getNumChannels(), std::min(oldSide.getNumChannels(), newSide.getNumChannels())));

    for (int channel = 0; channel < numChannels; ++channel) {
        const auto row = static_cast<std::size_t>(channel);
        auto* out = dest.getChannelPointer(row);
        const auto* before = oldSide.getChannelPointer(row);
        const auto* after = newSide.getChannelPointer(row);

        // Read both sides at an index before writing it, so a destination
        // sharing a buffer with either of them is no different from one that
        // does not. The first sample of a fade is the old signal exactly.
        for (int sample = 0; sample < fading; ++sample) {
            const auto gain = static_cast<float>(position + sample) / static_cast<float>(length_);
            out[sample] = before[sample] + ((after[sample] - before[sample]) * gain);
        }

        // Past the end of the ramp within the same block. Copying rather than
        // leaving it to the caller is what keeps a straddling block from being
        // a case anyone has to think about.
        if (fading < numSamples && out != after)
            juce::FloatVectorOperations::copy(out + fading, after + fading, numSamples - fading);
    }

    remaining_.store(left - fading, std::memory_order_relaxed);
}

const std::shared_ptr<AudioDelayLine>& PlanExecutor::audioDelayFor(OpId op) const {
    static const std::shared_ptr<AudioDelayLine> none;
    const auto line = audioDelayForOp_[static_cast<std::size_t>(op)];
    return line < 0 ? none : audioDelays_[static_cast<std::size_t>(line)];
}

const std::shared_ptr<MidiDelayLine>& PlanExecutor::midiDelayFor(OpId op) const {
    static const std::shared_ptr<MidiDelayLine> none;
    const auto line = midiDelayForOp_[static_cast<std::size_t>(op)];
    return line < 0 ? none : midiDelays_[static_cast<std::size_t>(line)];
}

const std::shared_ptr<CrossfadeRamp>& PlanExecutor::crossfadeFor(OpId op) const {
    static const std::shared_ptr<CrossfadeRamp> none;
    const auto ramp = crossfadeForOp_[static_cast<std::size_t>(op)];
    return ramp < 0 ? none : crossfades_[static_cast<std::size_t>(ramp)];
}

std::vector<char> PlanExecutor::unfinishedCrossfades() const {
    std::vector<char> running(plan_ == nullptr ? 0 : plan_->ops.size(), 0);
    for (std::size_t i = 0; i < running.size(); ++i) {
        const auto ramp = crossfadeForOp_[i];
        running[i] = ramp >= 0 && !crossfades_[static_cast<std::size_t>(ramp)]->spent() ? 1 : 0;
    }
    return running;
}

int PlanExecutor::activeCrossfades() const {
    int running = 0;
    for (const auto& ramp : crossfades_)
        running += ramp->spent() ? 0 : 1;
    return running;
}

void PlanExecutor::reset() {
    plan_ = nullptr;
    planFingerprint_ = 0;
    latencySamples_ = 0;
    carriedDelayLines_ = 0;
    carriedCrossfades_ = 0;
    crossfadeForOp_.clear();
    crossfades_.clear();
    audioSlots_.clear();
    midiSlots_.clear();
    slotChannels_.clear();
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
    meterForOp_.clear();
    boundMeterCount_ = 0;
    midiTapForOp_.clear();
    paramWindowForOp_.clear();
    mixerParamForOp_.clear();
    paramScratch_.clear();
    paramSegments_.clear();
    paramValues_.prepare(0);
    mods_.reset();
    paramLayout_ = 0;
}

std::vector<std::string> PlanExecutor::prepare(const RenderPlan& plan, const PlanBindings& bindings,
                                               const RenderContext& context,
                                               const PlanExecutor* previous,
                                               const ParamTable* params) {
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
    meterForOp_.assign(numOps, nullptr);
    midiTapForOp_.assign(numOps, nullptr);
    paramWindowForOp_.assign(numOps, ParamTable::DeviceWindow{});
    mixerParamForOp_.assign(numOps, OpMixerParams{});
    paramLayout_ = 0;

    // The parameters of the table this plan is published with (#2117). Sized
    // here, on this thread, so the block that resolves them allocates nothing,
    // and looked up here so the block that reads them hashes nothing. A plan
    // published without a table renders devices with no parameters, which is
    // what every render did before there was a table at all.
    if (params != nullptr) {
        paramLayout_ = params->layoutFingerprint;
        paramValues_.prepare(params->size());
        paramScratch_.assign(static_cast<std::size_t>(std::max(params->maxLinksPerParam, 0)),
                             ModContribution{});
        paramSegments_.assign(static_cast<std::size_t>(paramValues_.segmentCapacity()),
                              ParamSegment{});

        // The modifier engines, and whatever the epoch being replaced has
        // already turned. Shared rather than copied: the audio thread is still
        // inside that executor until the swap, so an LFO carried across one
        // goes on from where it is rather than from where it was read.
        mods_.prepare(*params, context, previous == nullptr ? nullptr : &previous->mods_);

        for (std::size_t i = 0; i < numOps; ++i) {
            const auto& op = plan.ops[i];
            if (op.kind == OpKind::Device) {
                paramWindowForOp_[i] = params->windowFor(op.key.deviceKey());
                continue;
            }

            // The two mixer ops a lane can play over. A rack chain's fader and
            // a rack's output are not addressable in the model, so they keep
            // the published value and are not looked up.
            ParamKey key;
            key.scope = ParamKey::Scope::Track;
            key.trackId = op.key.trackId;

            if (op.kind == OpKind::Fader && op.key.role == OpRole::TrackFader) {
                key.kind = ParamKey::Kind::TrackVolume;
                mixerParamForOp_[i].gain = params->find(key);
                key.kind = ParamKey::Kind::TrackPan;
                mixerParamForOp_[i].pan = params->find(key);
            } else if (op.kind == OpKind::SendTap) {
                key.kind = ParamKey::Kind::SendLevel;
                key.index = op.key.index;
                mixerParamForOp_[i].gain = params->find(key);
            }
        }
    }

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
                const auto found = bindings.devices.find(op.key.deviceKey());
                if (found == bindings.devices.end() || found->second == nullptr) {
                    // Passing audio through is what the current engine does
                    // with a plugin that failed to load, and it keeps the rest
                    // of the chain testable instead of silencing the track.
                    messages.push_back(describe(i) + "no device bound for " +
                                       toString(op.key.deviceKey()) + ", it passes audio through");
                    break;
                }
                deviceForOp_[i] = found->second;
                break;
            }

            case OpKind::Meter: {
                // Silently optional, unlike every other binding above. A meter
                // is the one op whose runtime object exists for something
                // outside the render: unbound, the op still passes its audio
                // on, and the only thing missing is a display nobody asked for.
                // Reporting it would put a line per track and per device slot
                // in front of every host that renders without a mixer, which is
                // every offline render there is.
                const auto found = bindings.meters.find(op.key);
                if (found == bindings.meters.end() || found->second == nullptr)
                    break;
                meterForOp_[i] = found->second;
                ++boundMeterCount_;
                break;
            }

            case OpKind::MergeMidi: {
                // Optional for the same reason a meter is, and silent for the
                // same reason: an observation point nobody is watching is the
                // ordinary case, and reporting one would put a line per track in
                // front of every render that is not being observed.
                const auto found = bindings.midiTaps.find(op.key);
                if (found == bindings.midiTaps.end() || found->second == nullptr)
                    break;
                midiTapForOp_[i] = found->second;
                break;
            }

            default:
                break;
        }
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

    // Through resolveLayout rather than the three passes inline, because the
    // plan goldens (#2076) pin what a prepare resolves and have to be resolving
    // it the same way. Two callers running the same three lines is how a step
    // added to one of them silently stops being pinned by the other.
    const auto prepared = resolveLayout(plan, deviceLatency);
    const auto& latency = prepared.latency;
    const auto& layout = prepared.buffers;

    portOffsets_ = prepared.portOffsets;
    latencySamples_ = latency.outputLatency;
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
            // A delay's output block covers one block's worth of its input's
            // time, and that stretch falls across two of the spans the budget
            // is counted over unless the delay is a whole number of them. Zero
            // is not a delay at all: the op does not run, and the port is its
            // input's.
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
    //
    // A line the differ matched is taken over rather than built, so what was in
    // flight when the plan changed comes out of the new one instead of being
    // replaced by that many samples of silence. Taken over means shared: the
    // audio thread is still rendering through the executor it came from, so
    // preparing it here would clear a ring being read. Anything that cannot be
    // shared as it stands is built fresh beside it and the old one goes when
    // its epoch does.
    // Not this executor: reset() has already emptied what a carry would read,
    // and an executor cannot take over from itself in any case. A caller
    // re-preparing in place is starting again, and gets what it asked for.
    const auto carry = previous != nullptr && previous != this && previous->plan_ != nullptr
                           ? diffPlans(*previous->plan_, plan)
                           : PlanDiff{};

    const auto carriedFrom = [&carry](std::size_t op) {
        return carry.carriedFrom.empty() ? INVALID_OP_ID : carry.carriedFrom[op];
    };

    audioDelayForOp_.assign(numOps, -1);
    midiDelayForOp_.assign(numOps, -1);
    for (std::size_t i = 0; i < numOps; ++i) {
        const auto samples = latency.delaySamples[i];
        if (plan.ops[i].kind != OpKind::Delay || samples <= 0)
            continue;

        const auto from = carriedFrom(i);

        if (plan.ops[i].outputs.front() == SignalKind::Audio) {
            std::shared_ptr<AudioDelayLine> line;
            if (from != INVALID_OP_ID)
                if (const auto& adopted = previous->audioDelayFor(from);
                    adopted != nullptr && adopted->hasConfiguration(context_.numChannels, samples,
                                                                    context_.maxBlockSize)) {
                    line = adopted;
                    ++carriedDelayLines_;
                }

            if (line == nullptr) {
                line = std::make_shared<AudioDelayLine>();
                line->prepare(context_.numChannels, samples, context_.maxBlockSize);
            }

            audioDelayForOp_[i] = static_cast<int>(audioDelays_.size());
            audioDelays_.push_back(std::move(line));
            continue;
        }

        // Everything the delay spans is in flight at once, counted in the spans
        // the port's budget is a budget over rather than in callbacks: how many
        // callbacks that stretch of timeline arrives in is the host's business
        // and would make this the wrong size. Two spare covers the one being
        // filled and the one being drained, neither of which lines up with
        // those spans.
        const auto blocks = samples / context_.maxBlockSize + 2;
        const auto capacity = blocks * portMidiBytes[flatPort(plan.ops[i].inputs.front())];

        std::shared_ptr<MidiDelayLine> line;
        if (from != INVALID_OP_ID)
            if (const auto& adopted = previous->midiDelayFor(from);
                adopted != nullptr && adopted->hasConfiguration(samples, capacity)) {
                line = adopted;
                ++carriedDelayLines_;
            }

        if (line == nullptr) {
            line = std::make_shared<MidiDelayLine>();
            line->prepare(samples, capacity);
        }

        midiDelayForOp_[i] = static_cast<int>(midiDelays_.size());
        midiDelays_.push_back(std::move(line));
    }

    // Crossfades, on the edges the pass that built this plan decided had moved.
    // A fade carried mid-ramp is shared with the executor it came from, exactly
    // as a delay line is: it is still running there until the swap, and the
    // whole point of re-emitting one is that it picks up where it had got to.
    const auto fadeSamples =
        std::max(1, static_cast<int>(std::lround(kCrossfadeSeconds * context_.sampleRate)));

    crossfadeForOp_.assign(numOps, -1);
    for (std::size_t i = 0; i < numOps; ++i) {
        if (plan.ops[i].kind != OpKind::Crossfade)
            continue;

        // The two sides have to arrive together. Where they do not, the edit
        // moved latency along the edge, and the compensation for the new one is
        // a delay line that starts flushed: fading into it would fade in from
        // that many samples of silence, which is louder than the step it was
        // there to smooth. The op stays and passes the new side through.
        if (latency.portLatency[flatPort(plan.ops[i].inputs[0])] !=
            latency.portLatency[flatPort(plan.ops[i].inputs[1])])
            continue;

        std::shared_ptr<CrossfadeRamp> ramp;
        if (const auto from = carriedFrom(i); from != INVALID_OP_ID)
            if (const auto& adopted = previous->crossfadeFor(from);
                adopted != nullptr && adopted->hasConfiguration(fadeSamples)) {
                ramp = adopted;
                ++carriedCrossfades_;
            }

        if (ramp == nullptr) {
            ramp = std::make_shared<CrossfadeRamp>();
            ramp->prepare(fadeSamples);
        }

        crossfadeForOp_[i] = static_cast<int>(crossfades_.size());
        crossfades_.push_back(std::move(ramp));
    }

    silence_.setSize(context_.numChannels, context_.maxBlockSize, false, true, false);
    silence_.clear();
    noMidi_.clear();

    // The arena, as the render path sees it. Taken once, here, so that nothing
    // during a block goes through juce::AudioBuffer: making a block out of one
    // writes its isClear flag, and two ops reading the same port would be two
    // threads writing that byte at once.
    const auto channels = static_cast<std::size_t>(context_.numChannels);
    slotChannels_.assign((audioSlots_.size() + 1) * channels, nullptr);
    for (std::size_t slot = 0; slot < audioSlots_.size(); ++slot)
        for (std::size_t channel = 0; channel < channels; ++channel)
            slotChannels_[slot * channels + channel] =
                audioSlots_[slot].getWritePointer(static_cast<int>(channel));
    for (std::size_t channel = 0; channel < channels; ++channel)
        slotChannels_[audioSlots_.size() * channels + channel] =
            silence_.getWritePointer(static_cast<int>(channel));

    planFingerprint_ = magda::engine::planFingerprint(plan);
    plan_ = &plan;
    return messages;
}

juce::dsp::AudioBlock<float> PlanExecutor::audioBlock(std::size_t row, int numSamples) const {
    const auto channels = static_cast<std::size_t>(context_.numChannels);
    return juce::dsp::AudioBlock<float>(&slotChannels_[row * channels], channels,
                                        static_cast<std::size_t>(numSamples));
}

juce::dsp::AudioBlock<float> PlanExecutor::audioIn(const PortRef& ref, int numSamples) const {
    // An unconnected slot reads the silence row, which is the last one and is
    // the reason the array has one more row than there are slots.
    return audioBlock(ref.valid() ? static_cast<std::size_t>(slotFor(ref)) : audioSlots_.size(),
                      numSamples);
}

juce::dsp::AudioBlock<float> PlanExecutor::audioOut(OpId op, int port, int numSamples) const {
    return audioBlock(static_cast<std::size_t>(slotFor(PortRef{op, port})), numSamples);
}

const juce::MidiBuffer& PlanExecutor::midiIn(const PortRef& ref) const {
    return ref.valid() ? midiSlots_[static_cast<std::size_t>(slotFor(ref))] : noMidi_;
}

juce::MidiBuffer& PlanExecutor::midiOut(OpId op, int port) {
    return midiSlots_[static_cast<std::size_t>(slotFor(PortRef{op, port}))];
}

PlanExecutor::BlockStart PlanExecutor::beginBlock(const PlanValues& values,
                                                  const BlockInfo& requested,
                                                  juce::AudioBuffer<float>& output) const {
    BlockStart start;
    output.clear();
    if (plan_ == nullptr)
        return start;

    // A host asking for more than the block the plan was prepared for would
    // otherwise leave the tail of its buffer silent with nothing saying so.
    jassert(requested.numSamples <= context_.maxBlockSize);

    start.block = requested;
    start.block.numSamples = std::min(requested.numSamples, context_.maxBlockSize);
    if (start.block.numSamples <= 0)
        return start;

    // Values are resolved against a plan and published separately from it, so
    // a table can outlive the plan it was made for. Matching op counts prove
    // nothing: a structural edit can replace ops and keep the count, and the
    // stale gains and mutes would land on whatever op now holds each index.
    // The fingerprint is what says the two belong together; without it, unity
    // is the safe reading.
    start.applyValues = appliesValues(values);
    start.render = true;
    return start;
}

void PlanExecutor::resolveParameters(const PlanValues& values, const BlockInfo& block) {
    // Two shapes have to match, and neither is something appliesValues can
    // see: it compares the plan's fingerprint and its op count, and a link
    // edit changes neither. A macro gaining its first link grows the table by
    // a slot, and a link added to the parameter that already had the most
    // widens the room one parameter's contributions are gathered in. Both
    // arrive on a table that belongs to this plan and neither fits what was
    // allocated for it.
    //
    // Empty rather than stale, and empty rather than partly applied: a device
    // gets a window of nothing, which is what it had before a table was ever
    // published, and the session escalates a publish of this shape into a
    // structural one so the emptiness lasts a block rather than for ever.
    if (!appliesValues(values) || values.params == nullptr || !fitsParameters(values)) {
        paramValues_.beginBlock(block.numSamples);
        return;
    }

    resolveParams(*values.params, paramValues_, paramScratch_, paramSegments_, block, &mods_);
}

void PlanExecutor::process(const PlanValues& values, const BlockInfo& requestedBlock,
                           juce::AudioBuffer<float>& output) {
    const auto start = beginBlock(values, requestedBlock, output);
    resolveParameters(values, start.block);
    if (!start.render)
        return;

    // In order, which is a schedule: ops are in dependency order, so everything
    // an op reads has already run by the time the walk reaches it.
    for (std::size_t i = 0; i < plan_->ops.size(); ++i)
        renderOp(static_cast<OpId>(i), start.applyValues ? values.ops[i] : kUnityValue, start.block,
                 output);
}

OpValue PlanExecutor::mixerValueFor(std::size_t op, const OpValue& published) const {
    const auto params = mixerParamForOp_[op];
    if (params.gain == INVALID_PARAM_ID)
        return published;

    const auto level = paramValues_[params.gain];
    if (level.empty())
        return published;

    // Silence is the published table's to say: mute and solo are not
    // parameters, nothing plays a lane over them, and a fader that moved does
    // not make a muted track audible.
    OpValue value = published;

    const auto gain = faderGainFromDecibels(level.value());

    // A fader carries both or neither (ParamTableCompiler::allocateMixer), so
    // a pan that is missing here is a send, which has none. A send's level is
    // one number and reaches both channels alike.
    const auto pan = params.pan == INVALID_PARAM_ID ? ParamValues{} : paramValues_[params.pan];

    if (pan.empty()) {
        value.gainLeft = gain;
        value.gainRight = gain;
    } else {
        applyLinearPanLaw(gain, faderPanPosition(pan.value()), value.gainLeft, value.gainRight);
    }

    return value;
}

void PlanExecutor::renderOp(OpId id, const OpValue& published, const BlockInfo& block,
                            juce::AudioBuffer<float>& output) {
    const auto i = static_cast<std::size_t>(id);
    const auto& op = plan_->ops[i];
    const auto numSamples = block.numSamples;
    const auto value = mixerValueFor(i, published);

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
                audioDelays_[static_cast<std::size_t>(line)]->process(out, numSamples);
                break;
            }

            const auto line = midiDelayForOp_[i];
            if (line < 0)
                break;
            auto& out = midiOut(id, 0);
            out.clear();
            midiDelays_[static_cast<std::size_t>(line)]->process(midiIn(op.inputs[0]), out,
                                                                 numSamples);
            jassert(out.data.size() <=
                    midiByteBounds_[static_cast<std::size_t>(slotFor(PortRef{id, 0}))]);
            break;
        }

        case OpKind::Crossfade: {
            auto out = audioOut(id, 0, numSamples);
            const auto newSide = audioIn(op.inputs[1], numSamples);
            const auto ramp = crossfadeForOp_[i];

            // Runs whatever the value layer says about the ops around it, for
            // the reason a delay does: a fade that stopped while its chain was
            // muted would come back mid-ramp when the chain did, minutes later,
            // fading between two signals nobody is still expecting to hear
            // change. A fade with no ramp is one the prepare refused, and a
            // spent one is an edge that has become what it was fading to.
            if (ramp < 0 || crossfades_[static_cast<std::size_t>(ramp)]->spent()) {
                if (!writesInPlace(i))
                    out.copyFrom(newSide);
                break;
            }

            crossfades_[static_cast<std::size_t>(ramp)]->process(
                out, audioIn(op.inputs[0], numSamples), newSide, numSamples);
            break;
        }

        case OpKind::MergeMidi: {
            auto& out = midiOut(id, 0);
            out.clear();
            if (!value.silent)
                for (const auto& input : op.inputs)
                    if (input.valid())
                        out.addEvents(midiIn(input), 0, numSamples, 0);

            // After the merge and on silent blocks too, so a tap reads what the
            // chain reads rather than what the executor felt like reporting: a
            // gated chain receives nothing, and that is a fact about the block
            // rather than an absence of one.
            if (auto* tap = midiTapForOp_[i]; tap != nullptr)
                tap->write(out, block);
            break;
        }

        case OpKind::Device: {
            auto audio = audioOut(id, 0, numSamples);
            const auto producesMidi = op.outputs.size() > 1 && op.outputs[1] == SignalKind::Midi;
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

            // No parameters yet: the table a device reads is resolved against a
            // plan and published with it, which is #2117. Until then a device
            // is handed an empty window rather than a stale one.
            const auto window = paramWindowForOp_[static_cast<std::size_t>(i)];
            DeviceBlock deviceBlock{.audio = audio,
                                    .midiIn = &midiIn(op.inputs[1]),
                                    .midiOut = deviceMidiOut,
                                    .sidechain = {},
                                    .params = paramValues_.device(window.first, window.count),
                                    .block = block};
            if (op.inputs[2].valid())
                deviceBlock.sidechain = audioIn(op.inputs[2], numSamples);
            device->process(deviceBlock);
            jassert(deviceMidiOut == nullptr || deviceMidiOut->data.size() <= kMaxMidiBytesPerPort);
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

            // Written on silent blocks too, though a maximum takes nothing
            // from them. What that buys is that the tap is only ever
            // cleared by being read, so how fast a meter falls is the
            // reader's cadence and not which blocks the executor bothered
            // to report.
            if (auto* tap = meterForOp_[i]; tap != nullptr)
                tap->write(out, numSamples);
            break;
        }

        case OpKind::Output: {
            if (value.silent || !op.inputs[0].valid())
                break;
            auto in = audioIn(op.inputs[0], numSamples);
            const auto numChannels =
                std::min(static_cast<int>(in.getNumChannels()), output.getNumChannels());
            for (int channel = 0; channel < numChannels; ++channel)
                output.addFrom(channel, 0, in.getChannelPointer(static_cast<std::size_t>(channel)),
                               numSamples);
            break;
        }
    }
}

}  // namespace magda::engine
