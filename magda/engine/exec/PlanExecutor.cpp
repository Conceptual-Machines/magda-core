#include "exec/PlanExecutor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
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

/**
 * @brief What an audio trigger listens for.
 *
 * The fork's own three numbers (AudioSidechainMonitorPlugin), because a project
 * that ducks on a kick has to duck on the same kicks in both engines. A fast
 * attack so a transient opens the gate, a moderate release so it does not
 * chatter at the boundary, and a threshold around -20 dB, which is above what a
 * quiet track leaves behind and below anything meant as a hit.
 *
 * One detector per source track rather than one per modifier, which is the
 * fork's arrangement too: what an audio trigger keys off is a property of the
 * signal rather than of whatever is listening to it.
 */
constexpr float kTriggerThreshold = 0.1f;
constexpr double kTriggerAttackMs = 1.0;
constexpr double kTriggerReleaseMs = 50.0;

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
    paramTaps_.clear();
    modTaps_.clear();
    unboundTaps_.clear();
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
    insertForOp_.assign(numOps, nullptr);
    audioSourceForOp_.assign(numOps, nullptr);
    midiSourceForOp_.assign(numOps, nullptr);
    meterForOp_.assign(numOps, nullptr);
    midiTapForOp_.assign(numOps, nullptr);
    paramWindowForOp_.assign(numOps, ParamTable::DeviceWindow{});
    mixerParamForOp_.assign(numOps, OpMixerParams{});
    modSourceForOp_.assign(numOps, std::span<const int>{});

    // The detectors of the epoch being replaced, by the track each watches.
    // A modulation tap's envelope is state the way an LFO's phase is state, and
    // a structural republish during playback must not zero it: a source above
    // the threshold at that moment would read as a rising edge on the next
    // block and fire a trigger nothing played. The fork's monitor plugin
    // survives the same edits with its own level and gate intact.
    // Shared rather than copied, on the terms the delay lines and the modifier
    // states are shared: the executor being replaced is still rendering while
    // this one is prepared, and a tap's envelope is written on the audio thread
    // by exactly that render. Copying it here would be a read racing a write,
    // and would carry a half-updated detector, which is the spurious edge the
    // carry exists to prevent.
    // Keyed by the tap rather than by the track, because a track has two of
    // them: one where its triggers key off and one where its followers listen.
    // The track alone would collapse the pair onto one entry, and the plan
    // taking that over would run both taps through a single detector, so a
    // level at one point would open and shut the gate at the other.
    std::map<std::pair<TrackId, int>, std::shared_ptr<TriggerDetector>> carriedTriggers;
    if (previous != nullptr && previous != this && previous->plan_ != nullptr)
        for (std::size_t i = 0; i < previous->plan_->ops.size(); ++i)
            if (const auto& op = previous->plan_->ops[i]; op.kind == OpKind::ModSource)
                carriedTriggers[{op.key.trackId, op.key.index}] = previous->triggerForOp_[i];

    triggerForOp_.assign(numOps, nullptr);
    carriedTriggerDetectors_ = 0;
    for (std::size_t i = 0; i < numOps; ++i) {
        if (plan.ops[i].kind != OpKind::ModSource)
            continue;

        if (const auto found =
                carriedTriggers.find({plan.ops[i].key.trackId, plan.ops[i].key.index});
            found != carriedTriggers.end() && found->second != nullptr) {
            triggerForOp_[i] = found->second;
            ++carriedTriggerDetectors_;
        } else {
            triggerForOp_[i] = std::make_shared<TriggerDetector>();
        }
    }

    inMidiPrefix_.assign(numOps, 0);
    midiPrefix_.clear();
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
            } else if (op.kind == OpKind::Fader && op.key.role == OpRole::RackChainFader &&
                       op.padLevelParam >= 0) {
                // The exception to the line above: a Drum Grid's pad level and
                // pan are real parameters of the device that owns the pad, so a
                // lane over them has to reach this fader. Read through the
                // owner's window, which is indexed by the device's own
                // parameter index.
                const auto window = params->windowFor(op.key.deviceKey());
                const auto slot = [&](int index) {
                    return index >= 0 && index < window.count
                               ? static_cast<ParamId>(window.first + index)
                               : INVALID_PARAM_ID;
                };
                mixerParamForOp_[i].gain = slot(op.padLevelParam);
                mixerParamForOp_[i].pan = slot(op.padPanParam);
            } else if (op.kind == OpKind::SendTap) {
                key.kind = ParamKey::Kind::SendLevel;
                key.index = op.key.index;
                mixerParamForOp_[i].gain = params->find(key);
            } else if (op.kind == OpKind::ModSource) {
                // The far end of the edge the compiler emitted, resolved once
                // here. Both compilers worked the routing out from the same
                // rule (ModSources.hpp), so a tap with nothing on the far end
                // is the two having drifted rather than a project that has one.
                modSourceForOp_[i] = mods_.listenersOf(op.key.trackId);
            }
        }
    }

    // The values a host asked to read back, resolved to the indices the block
    // already has (#2122). A key with a parameter index is a parameter of the
    // table; a modifier key has none and is looked for in the runtime, which is
    // where the output it names is published.
    //
    // A key neither the table nor the runtime has binds nothing and is not
    // reported, which is the ordinary answer rather than a fault: the table
    // carries a mixer value, a macro or a modifier's rate only while something
    // reaches it, so a host drawing an unautomated fader is asking about a
    // number the engine does not move. It is kept anyway, because the swap has
    // to clear it: a tap that used to be published and now is not would
    // otherwise freeze where the last plan left it.
    //
    // Outside the block above rather than inside it, because a plan published
    // with no table at all is the same situation seen from further away. It
    // publishes nothing, so every tap the bindings offered is one of these; a
    // loop that only ran when there was a table would leave them all frozen at
    // whatever the last plan that had one wrote.
    for (const auto& [key, tap] : bindings.valueTaps) {
        if (tap == nullptr)
            continue;

        if (params != nullptr) {
            if (const auto param = params->find(key); param != INVALID_PARAM_ID) {
                paramTaps_.push_back(BoundValueTap{param, tap});
                continue;
            }

            if (const auto modifier = mods_.indexOf(key); modifier >= 0) {
                modTaps_.push_back(BoundValueTap{modifier, tap});
                continue;
            }
        }

        unboundTaps_.push_back(tap);
    }

    // Which ops the block can render before it resolves anything. In plan
    // order, which is dependency order, so a producer is decided before the ops
    // reading it are asked about (PlanExecutor.hpp says what this buys).
    for (std::size_t i = 0; i < numOps; ++i) {
        const auto& op = plan.ops[i];

        const bool onlyMidi =
            !op.outputs.empty() && std::ranges::all_of(op.outputs, [](const PortDesc& port) {
                return port.kind == SignalKind::Midi;
            });

        const bool fedByPrefix = std::ranges::all_of(op.inputs, [&](const PortRef& input) {
            return !input.valid() || inMidiPrefix_[static_cast<std::size_t>(input.op)] != 0;
        });

        if (!onlyMidi || !fedByPrefix)
            continue;

        inMidiPrefix_[i] = 1;
        midiPrefix_.push_back(static_cast<OpId>(i));
    }

    detectMono_.assign(static_cast<std::size_t>(std::max(context.maxBlockSize, 0)), 0.0f);

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

            // Reported only by a host that has a launcher at all, which is what
            // a non-empty binding map says. A session op is emitted for every
            // clip-carrying track whether or not the project has a session
            // (#2301), so binding none of them is an ordinary configuration.
            // Binding some and not others is a track that lost its source.
            case OpKind::SessionAudio:
                audioSourceForOp_[i] = findAudioSource(bindings.sessionAudio, trackId);
                if (audioSourceForOp_[i] == nullptr && !bindings.sessionAudio.empty())
                    messages.push_back(describe(i) + "no session audio source bound for track " +
                                       std::to_string(trackId) + ", it renders silence");
                break;

            case OpKind::SessionMidi:
                midiSourceForOp_[i] = findMidiSource(bindings.sessionMidi, trackId);
                if (midiSourceForOp_[i] == nullptr && !bindings.sessionMidi.empty())
                    messages.push_back(describe(i) + "no session MIDI source bound for track " +
                                       std::to_string(trackId) + ", it renders nothing");
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

            case OpKind::InsertSend:
            case OpKind::InsertReturn: {
                // Both halves resolve to one object, because a round trip is
                // one thing: an implementation handed a separately bound sink
                // and source would be rebuilding the pairing the plan already
                // did (#2245).
                const auto found = bindings.inserts.find(op.key.deviceKey());
                if (found == bindings.inserts.end() || found->second == nullptr) {
                    // Reported, unlike a device that failed to load, which
                    // passes audio through. There is no passing through here:
                    // an insert with nothing behind it sends into nothing and
                    // its return has nothing to answer with, so a render that
                    // went ahead would be a render of a project with the
                    // outboard unplugged and no way to tell from the audio.
                    messages.push_back(describe(i) + "no insert bound for " +
                                       toString(op.key.deviceKey()) +
                                       ", nothing leaves the machine and its return is silent");
                    break;
                }
                insertForOp_[i] = found->second;
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
    for (std::size_t i = 0; i < numOps; ++i) {
        if (const auto* device = deviceForOp_[i]; device != nullptr)
            deviceLatency[i] = device->latencySamples();

        // An insert's round trip goes in the same array, on the return op,
        // which is where it is incurred: what left is coming back late, and
        // everything downstream of the return is what has to be aligned against
        // it. The send declares none -- nothing waits on it, because nothing
        // reads it -- and the latency pass below has no idea any of this is an
        // insert, which is the whole point (#2245).
        if (plan.ops[i].kind == OpKind::InsertReturn)
            if (const auto* insert = insertForOp_[i]; insert != nullptr)
                deviceLatency[i] = insert->latencySamples();
    }

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
            if (op.outputs.front().kind != SignalKind::Midi)
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
                    .outputs[static_cast<std::size_t>(input.port)]
                    .kind != SignalKind::Midi)
                continue;
            carried += portMidiBytes[flatPort(input)];
        }

        for (std::size_t port = 0; port < op.outputs.size(); ++port)
            if (op.outputs[port].kind == SignalKind::Midi)
                portMidiBytes[static_cast<std::size_t>(portOffsets_[i]) + port] = carried;
    }

    midiSlots_.resize(static_cast<std::size_t>(layout.numMidiSlots));
    midiByteBounds_.assign(static_cast<std::size_t>(layout.numMidiSlots), 0);
    for (std::size_t i = 0; i < numOps; ++i) {
        for (std::size_t port = 0; port < plan.ops[i].outputs.size(); ++port) {
            if (plan.ops[i].outputs[port].kind != SignalKind::Midi)
                continue;
            const auto flat = static_cast<std::size_t>(portOffsets_[i]) + port;
            auto& bound = midiByteBounds_[static_cast<std::size_t>(portSlots_[flat])];
            bound = std::max(bound, portMidiBytes[flat]);
        }
    }

    for (std::size_t slot = 0; slot < midiSlots_.size(); ++slot)
        midiSlots_[slot].ensureSize(static_cast<std::size_t>(midiByteBounds_[slot]));

    // Tell every bound device how much MIDI can reach it, now that the sums are
    // known and while there is still a thread that may allocate.
    //
    // A device that buffers its input cannot work this out for itself:
    // kMaxMidiBytesPerPort is what one producer may write, and a device fed by
    // a merge is fed the sum of several. Sizing from the constant is how a
    // device on a track with two MIDI sources silently drops the second one's
    // tail. The executor is the only thing that knows, so it is the thing that
    // says.
    for (std::size_t i = 0; i < numOps; ++i) {
        auto* device = deviceForOp_[i];
        if (device == nullptr)
            continue;

        const auto& op = plan.ops[i];
        const auto hasMidiIn = op.inputs.size() > 1 && op.inputs[1].valid();
        device->setMidiInputBoundBytes(
            hasMidiIn ? midiByteBounds_[static_cast<std::size_t>(slotFor(op.inputs[1]))] : 0);
    }

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

        if (plan.ops[i].outputs.front().kind == SignalKind::Audio) {
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

void PlanExecutor::settleBlockTable(const PlanValues& values) {
    // Null on a block whose values do not fit, which is the same block that
    // resolves nothing: a modifier addressed through the wrong table is
    // another modifier, and half a table is worse than none.
    blockTable_ = appliesValues(values) && fitsParameters(values) ? values.params.get() : nullptr;
}

void PlanExecutor::renderMidiPrefix(const PlanValues& values, const BlockInfo& block) {
    settleBlockTable(values);

    if (plan_ == nullptr)
        return;

    // The ops themselves. Nothing here reads a parameter, so a block whose
    // values do not apply renders exactly the same MIDI: what an op of this set
    // reads is a clip, an input queue, or another op of this set.
    const bool applyValues = appliesValues(values);
    for (const auto id : midiPrefix_)
        renderOp(id, applyValues ? values.ops[static_cast<std::size_t>(id)] : kUnityValue, block,
                 silence_);

    if (blockTable_ == nullptr)
        return;

    // And then the notes in them, spent before the resolve rather than after,
    // which is the whole point of running the prefix early: a modifier gated by
    // a note hears it in the block the note is in.
    for (std::size_t i = 0; i < plan_->ops.size(); ++i) {
        const auto& op = plan_->ops[i];
        if (op.kind != OpKind::ModSource || modSourceForOp_[i].empty())
            continue;

        const auto& midi = op.inputs[1];
        if (midi.valid() && inMidiPrefix_[static_cast<std::size_t>(midi.op)] != 0)
            feedNoteTriggers(i, midiIn(midi));
    }
}

void PlanExecutor::feedNoteTriggers(std::size_t op, const juce::MidiBuffer& midi) {
    if (midi.isEmpty())
        return;

    // Whether the block holds a note at all, settled before anything is fed.
    // A cross-track listener takes one trigger for the block rather than one
    // per note, which is what the fork's monitor does: it scans the buffer,
    // sets a flag, and fires triggerSidechain once.
    bool anyNoteOn = false;
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn()) {
            anyNoteOn = true;
            break;
        }

    for (const auto index : modSourceForOp_[op]) {
        if (mods_.listensFor(index, *blockTable_) != ModListen::Midi)
            continue;

        // A modifier living somewhere else is following this track rather than
        // playing it. The note-counting path refuses it by design, so a trigger
        // is the only door it has, and there is no note-off half: the fork
        // deliberately leaves the gate alone there so the modifier runs its
        // full cycle after a hit (SidechainMonitorPlugin says so at the note-off
        // branch it does not take).
        if (mods_.drivenFromElsewhere(index, *blockTable_)) {
            if (anyNoteOn)
                mods_.trigger(index, *blockTable_);
            continue;
        }

        // A modifier on the source hears its notes as its own, one at a time,
        // so the held count is right and the gate shuts when the last lifts.
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
                mods_.noteOn(index, *blockTable_);
            else if (message.isNoteOff(true))
                mods_.noteOff(index, *blockTable_);
            else if (message.isAllNotesOff() || message.isAllSoundOff())
                mods_.allNotesOff(index, *blockTable_);
        }
    }
}

void PlanExecutor::renderModSource(OpId id, const BlockInfo& block) {
    const auto i = static_cast<std::size_t>(id);
    const auto& op = plan_->ops[i];

    if (blockTable_ == nullptr || modSourceForOp_[i].empty())
        return;

    // The notes, where the prefix did not already spend them. A source whose
    // MIDI a device makes is not in the prefix, so its notes are read here and
    // land a block later, which is the same lag an audio trigger has.
    if (const auto& midi = op.inputs[1];
        midi.valid() && inMidiPrefix_[static_cast<std::size_t>(midi.op)] == 0)
        feedNoteTriggers(i, midiIn(midi));

    const auto& audio = op.inputs[0];
    if (!audio.valid())
        return;

    const auto numSamples = std::min(block.numSamples, static_cast<int>(detectMono_.size()));
    if (numSamples <= 0)
        return;

    // Downmixed to mono by averaging the channels, which is what the fork's tap
    // hands its followers. A sum would make a stereo source read six decibels
    // louder than the same material in mono.
    auto in = audioIn(audio, numSamples);
    const auto channels = static_cast<int>(in.getNumChannels());
    const auto scale = channels > 0 ? 1.0f / static_cast<float>(channels) : 0.0f;

    for (int s = 0; s < numSamples; ++s) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += in.getSample(c, s);
        detectMono_[static_cast<std::size_t>(s)] = sum * scale;
    }

    const auto mono =
        std::span<const float>{detectMono_}.first(static_cast<std::size_t>(numSamples));

    // The level a trigger keys off, which is the block's peak rather than the
    // mono average: the fork's monitor takes the loudest channel's magnitude
    // and a duck should follow whichever side is loud.
    float peak = 0.0f;
    for (int c = 0; c < channels; ++c)
        for (int s = 0; s < numSamples; ++s)
            peak = std::max(peak, std::abs(in.getSample(c, s)));

    auto* detector = triggerForOp_[i].get();
    if (detector == nullptr)
        return;

    // One envelope per source rather than one per modifier, and the constants
    // are the fork's: a fast attack so a transient opens the gate, a moderate
    // release so it does not chatter, and a threshold well above the noise a
    // silent track leaves behind. Per block, from the block's own length, so an
    // offline render detects the same hits as playback does.
    const auto blockMs = 1000.0 * numSamples / std::max(preparedContext().sampleRate, 1.0);
    const auto attack = 1.0f - std::exp(static_cast<float>(-blockMs / kTriggerAttackMs));
    const auto release = 1.0f - std::exp(static_cast<float>(-blockMs / kTriggerReleaseMs));

    detector->envelope +=
        (peak > detector->envelope ? attack : release) * (peak - detector->envelope);

    const bool rising = !detector->open && detector->envelope > kTriggerThreshold;
    const bool falling = detector->open && detector->envelope < kTriggerThreshold;
    if (rising || falling)
        detector->open = rising;

    // Which of a track's two taps this is. A listener reads the one it names,
    // so a follower at the far end and a trigger at the near one on the same
    // source each hear their own point rather than sharing whichever op ran.
    const auto point =
        op.key.index == 0 ? magda::ModTapPoint::PreFx : magda::ModTapPoint::PostFader;

    for (const auto index : modSourceForOp_[i]) {
        if (mods_.listensFor(index, *blockTable_) != ModListen::Audio)
            continue;
        if (blockTable_->modifiers[static_cast<std::size_t>(index)].tap != point)
            continue;

        // A follower wants the samples; a trigger wants the edge. Both listen
        // to the same track and neither is the other's fallback.
        if (blockTable_->modifiers[static_cast<std::size_t>(index)].kind == ModKind::Follower) {
            mods_.detectSource(index, *blockTable_, mono);
            continue;
        }

        if (rising)
            mods_.trigger(index, *blockTable_);
        else if (falling)
            mods_.setGated(index, *blockTable_, true);
    }
}

void PlanExecutor::resolveParameters(const PlanValues& values, const BlockInfo& block) {
    settleBlockTable(values);

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
    if (blockTable_ == nullptr) {
        paramValues_.beginBlock(block.numSamples);
        return;
    }

    resolveParams(*blockTable_, paramValues_, paramScratch_, paramSegments_, block, &mods_);
    publishValueTaps();
}

void PlanExecutor::clearUnboundValueTaps() {
    for (auto* tap : unboundTaps_)
        tap->clear();
}

void PlanExecutor::publishValueTaps() {
    // The position the parameter opens the block at, clamped, which is the same
    // answer a link reading it as a source gets. A knob draws where the value
    // is at the boundary for the same reason a device reads it there: that is
    // where the fork settles a parameter, and what is being drawn is what is
    // being heard.
    for (const auto& bound : paramTaps_)
        bound.tap->write(paramValues_.sourceValue(bound.index));

    // The modifier's own output rather than anything downstream of it, so a
    // modifier editor animates the shape the modifier made and not the depth
    // some link applied to it. The depth belongs to the link, and a parameter
    // tap is where its effect shows.
    for (const auto& bound : modTaps_)
        bound.tap->write(mods_.value(bound.index));
}

void PlanExecutor::process(const PlanValues& values, const BlockInfo& requestedBlock,
                           juce::AudioBuffer<float>& output) {
    const auto start = beginBlock(values, requestedBlock, output);
    if (!start.render) {
        resolveParameters(values, start.block);
        return;
    }

    // The MIDI a block has before it has any audio, and the triggers in it.
    // First, so that a note-gated modifier is resolved with the note that
    // arrived in this block rather than with the one before it.
    renderMidiPrefix(values, start.block);

    resolveParameters(values, start.block);

    // In order, which is a schedule: ops are in dependency order, so everything
    // an op reads has already run by the time the walk reaches it. The prefix
    // is skipped rather than re-run: rendering a live input source twice would
    // consume the queue twice and drop half the notes.
    for (std::size_t i = 0; i < plan_->ops.size(); ++i) {
        if (inMidiPrefix_[i] != 0)
            continue;

        renderOp(static_cast<OpId>(i), start.applyValues ? values.ops[i] : kUnityValue, start.block,
                 output);
    }
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
        case OpKind::AudioInput:
        case OpKind::SessionAudio: {
            auto out = audioOut(id, 0, numSamples);
            if (value.silent || audioSourceForOp_[i] == nullptr)
                out.clear();
            else
                audioSourceForOp_[i]->render(block, out);
            break;
        }

        case OpKind::ClipMidi:
        case OpKind::MidiInput:
        case OpKind::SessionMidi: {
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

        case OpKind::Subtract: {
            // What the processing added: its output, minus the dry signal it
            // was handed, which the compiler has already aligned to it. Whether
            // anything is taken away is the value layer's to say, and while it
            // says no this is a passthrough, one the buffer assignment has
            // usually already let write in place. An unconnected dry side
            // leaves the output alone too, the way every other op here treats a
            // slot its arity declares and its plan did not fill.
            auto out = audioOut(id, 0, numSamples);
            if (value.silent || !op.inputs[0].valid()) {
                out.clear();
                break;
            }
            if (!writesInPlace(i))
                out.copyFrom(audioIn(op.inputs[0], numSamples));
            if (value.subtractsDry && op.inputs[1].valid())
                out.subtract(audioIn(op.inputs[1], numSamples));
            break;
        }

        case OpKind::Delay: {
            // A delay runs whatever the value layer says about the ops
            // around it. One that stopped writing while its chain was
            // silent would hand back audio from before the silence when
            // the chain returned, which is the one thing a delay line
            // cannot do.
            if (op.outputs.front().kind == SignalKind::Audio) {
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

        case OpKind::MidiNoteGate: {
            // A pad answers to a contiguous range of notes and plays them from
            // its own root, so a sampler mapped at C0 sounds from whichever pad
            // triggered it. Both halves are here because they are one decision:
            // an event is either this pad's, in which case it arrives
            // transposed, or it is not and this pad never sees it.
            auto& out = midiOut(id, 0);
            out.clear();

            if (value.silent)
                break;

            const int low = op.noteGateLow;
            const int high = op.noteGateHigh;
            const int transpose = op.noteGateTranspose;

            for (const auto metadata : midiIn(op.inputs[0])) {
                auto message = metadata.getMessage();

                // Everything that is not a note carries no pitch to gate on and
                // is the chain's business as much as any pad's: a sustain pedal
                // or a pitch bend reaches every pad, which is what the current
                // engine does with them too.
                if (!message.isNoteOnOrOff() && !message.isAftertouch()) {
                    out.addEvent(message, metadata.samplePosition);
                    continue;
                }

                const int note = message.getNoteNumber();
                if (note < low || note > high)
                    continue;

                // Clamped rather than dropped. A transposed note past the end of
                // the keyboard is a pad configured to play higher than MIDI
                // goes, and the note it asked for is the nearest one that
                // exists; dropping it would leave a pad that triggers nothing
                // and looks broken.
                message.setNoteNumber(std::clamp(note + transpose, 0, 127));
                out.addEvent(message, metadata.samplePosition);
            }

            if (auto* tap = midiTapForOp_[i]; tap != nullptr)
                tap->write(out, block);
            break;
        }

        case OpKind::Device: {
            auto audio = audioOut(id, 0, numSamples);
            const auto producesMidi =
                op.outputs.size() > 1 && op.outputs[1].kind == SignalKind::Midi;
            juce::MidiBuffer* deviceMidiOut = nullptr;
            if (producesMidi) {
                deviceMidiOut = &midiOut(id, 1);
                deviceMidiOut->clear();
            }
            // A multi-out instrument's further pairs sit on the ports after
            // the main audio and any MIDI, so the count is what is left.
            //
            // Cleared here, ahead of everything that can leave this op early,
            // rather than beside the call that hands them over. A pair the
            // device does not write this block, because the track is silent or
            // because nothing is bound, would otherwise give the track reading
            // it the block before.
            const auto firstMultiOutPort = producesMidi ? 2 : 1;
            const auto multiOutPairs = std::min(
                static_cast<int>(op.outputs.size()) - firstMultiOutPort, kMaxMultiOutPairs);
            for (int pair = 0; pair < multiOutPairs; ++pair)
                audioOut(id, firstMultiOutPort + pair, numSamples).clear();

            if (value.silent) {
                audio.clear();
                break;
            }

            if (!op.inputs[0].valid())
                audio.clear();
            else if (!writesInPlace(i))
                audio.copyFrom(audioIn(op.inputs[0], numSamples));

            // No device bound means pass the audio through unchanged, at full
            // width. The widths below describe a plugin, and there is none
            // here: the current engine answers 2 and 2 for a chain node whose
            // plugin it cannot find.
            auto* device = deviceForOp_[i];
            if (device == nullptr)
                break;

            // The device gets exactly the channels it declared, sized to the
            // wider of what it reads and what it writes. So a mono device sees
            // one channel instead of being handed stereo and read back as if
            // it had written both sides.
            const auto inputWidth =
                static_cast<std::size_t>(op.inputs[0].valid() ? op.audioInputChannels : 0);
            const auto outputWidth = static_cast<std::size_t>(op.outputs.front().channels);
            const auto blockWidth = std::max(inputWidth, outputWidth);

            // A channel the device writes but does not read starts silent. The
            // current engine leaves that pin unconnected on the way in, so a
            // one-in two-out device must not find the bus on its second
            // channel.
            for (auto channel = inputWidth; channel < blockWidth; ++channel)
                audio.getSingleChannelBlock(channel).clear();

            // No parameters yet: the table a device reads is resolved against a
            // plan and published with it, which is #2117. Until then a device
            // is handed an empty window rather than a stale one.
            const auto window = paramWindowForOp_[static_cast<std::size_t>(i)];
            DeviceBlock deviceBlock{.audio = audio.getSubsetChannelBlock(0, blockWidth),
                                    .midiIn = &midiIn(op.inputs[1]),
                                    .midiOut = deviceMidiOut,
                                    .sidechain = {},
                                    .params = paramValues_.device(window.first, window.count),
                                    .block = block};
            if (op.inputs[2].valid())
                deviceBlock.sidechain = audioIn(op.inputs[2], numSamples);

            // The pair blocks are gathered on the stack rather than on the
            // executor: the parallel executor runs Device ops on any thread it
            // likes, and anything shared here would be two devices writing one
            // array. Built only where there are pairs, so the devices that have
            // none, which is nearly all of them, pay nothing for it.
            if (multiOutPairs <= 0) {
                device->process(deviceBlock);
            } else {
                std::array<juce::dsp::AudioBlock<float>, kMaxMultiOutPairs> pairs;
                for (int pair = 0; pair < multiOutPairs; ++pair) {
                    const auto& port =
                        op.outputs[static_cast<std::size_t>(firstMultiOutPort + pair)];
                    pairs[static_cast<std::size_t>(pair)] =
                        audioOut(id, firstMultiOutPort + pair, numSamples)
                            .getSubsetChannelBlock(0, static_cast<std::size_t>(port.channels));
                }
                deviceBlock.extraOutputs = {pairs.data(), static_cast<std::size_t>(multiOutPairs)};
                device->process(deviceBlock);
            }

            // Everything downstream reads slots at full width, so each port
            // has to fill one. A mono port's channel is copied to both sides,
            // the way the current engine reads a one-pin source off pin 1
            // twice. A port carrying nothing is cleared, so it does not hand
            // on the input the buffer was filled with.
            for (std::size_t port = 0; port < op.outputs.size(); ++port) {
                const auto& output = op.outputs[port];
                if (output.kind != SignalKind::Audio || output.channels == 2)
                    continue;
                auto slot = audioOut(id, static_cast<int>(port), numSamples);
                if (output.channels == 1)
                    slot.getSingleChannelBlock(1).copyFrom(slot.getSingleChannelBlock(0));
                else
                    slot.clear();
            }
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
            if (op.outputs.size() > 1 && op.outputs[1].kind == SignalKind::Midi) {
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

        case OpKind::ModSource:
            renderModSource(id, block);
            break;

        case OpKind::InsertSend: {
            // A sink, and the one place in the graph where the block leaves the
            // machine. Silent means the chain feeding it is muted, and a muted
            // track sends nothing: the hardware hears what the track is doing,
            // which for a muted track is nothing (#2245).
            auto* insert = insertForOp_[i];
            if (insert == nullptr || value.silent)
                break;

            static const juce::MidiBuffer kNoMidi;

            // Either input may be unconnected: an external effect sends audio
            // and no MIDI, an external instrument the reverse. An empty block
            // and an empty buffer are what "this insert has no send of that
            // kind" looks like to whoever implements it.
            const auto audio =
                op.inputs[0].valid()
                    ? juce::dsp::AudioBlock<const float>(audioIn(op.inputs[0], numSamples))
                    : juce::dsp::AudioBlock<const float>();

            insert->send(block, audio, op.inputs[1].valid() ? midiIn(op.inputs[1]) : kNoMidi);
            break;
        }

        case OpKind::InsertReturn: {
            // A source, like a live input, and asked for exactly the one port
            // the plan declared: the compiler emits an audio port or a MIDI
            // port from what the insert says comes back, so which of the two is
            // wanted is already decided here.
            auto* insert = insertForOp_[i];
            const bool returnsAudio = op.outputs[0].kind == SignalKind::Audio;

            static thread_local juce::MidiBuffer discardedMidi;
            discardedMidi.clear();

            if (returnsAudio) {
                auto out = audioOut(id, 0, numSamples);
                if (insert == nullptr || value.silent)
                    out.clear();
                else
                    insert->receive(block, out, discardedMidi);
            } else {
                auto& out = midiOut(id, 0);
                out.clear();
                if (insert != nullptr && !value.silent)
                    insert->receive(block, {}, out);

                jassert(out.data.size() <= kMaxMidiBytesPerPort);
            }
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
