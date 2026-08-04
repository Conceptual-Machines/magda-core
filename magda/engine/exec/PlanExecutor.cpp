#include "exec/PlanExecutor.hpp"

#include <algorithm>
#include <set>

namespace magda::engine {
namespace {

/// Headroom for one block of MIDI on one port, reserved at prepare time so
/// addEvent() never grows a buffer on the audio thread. Generous: a block of
/// dense controller traffic is a few hundred bytes. The sample-accurate event
/// streams replace this with a proper reader.
constexpr int kMidiPortCapacityBytes = 8192;

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

void PlanExecutor::reset() {
    plan_ = nullptr;
    audioSlots_.clear();
    midiSlots_.clear();
    portOffsets_.clear();
    portSlots_.clear();
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

    // Flatten (op, port) to a buffer slot. Audio and MIDI ports are counted
    // separately; which array a slot indexes follows from the port's kind,
    // which the plan already carries.
    portOffsets_.assign(numOps + 1, 0);
    portSlots_.clear();
    int numAudioSlots = 0;
    int numMidiSlots = 0;
    for (std::size_t i = 0; i < numOps; ++i) {
        portOffsets_[i] = static_cast<int>(portSlots_.size());
        for (const auto kind : plan.ops[i].outputs)
            portSlots_.push_back(kind == SignalKind::Audio ? numAudioSlots++ : numMidiSlots++);
    }
    portOffsets_[numOps] = static_cast<int>(portSlots_.size());

    audioSlots_.resize(static_cast<std::size_t>(numAudioSlots));
    for (auto& buffer : audioSlots_)
        buffer.setSize(context_.numChannels, context_.maxBlockSize, false, true, false);

    midiSlots_.resize(static_cast<std::size_t>(numMidiSlots));
    for (auto& buffer : midiSlots_)
        buffer.ensureSize(kMidiPortCapacityBytes);

    silence_.setSize(context_.numChannels, context_.maxBlockSize, false, true, false);
    silence_.clear();
    noMidi_.ensureSize(kMidiPortCapacityBytes);
    noMidi_.clear();

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

        // Read but not acted on. Latency compensation is its own slice, and a
        // device that delays its output by samples nobody aligns is a phase
        // error in the mix rather than a missing feature nobody notices.
        if (const auto* device = deviceForOp_[i]; device != nullptr && device->latencySamples() > 0)
            messages.push_back(describe(i) + "device reports " +
                               std::to_string(device->latencySamples()) +
                               " samples of latency, which nothing compensates for yet");
    }

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

    auto block = requestedBlock;
    block.numSamples = std::min(block.numSamples, context_.maxBlockSize);
    const auto numSamples = block.numSamples;
    if (numSamples <= 0)
        return;

    // Values are resolved against a plan; one resolved against a different plan
    // would apply another op's gain to this one, so unity is the safe reading.
    static constexpr OpValue kUnity;
    const auto haveValues = values.ops.size() == plan_->ops.size();

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
                if (!value.silent && midiSourceForOp_[i] != nullptr)
                    midiSourceForOp_[i]->render(block, out);
                break;
            }

            case OpKind::MixAudio: {
                auto out = audioOut(id, 0, numSamples);
                out.clear();
                if (value.silent)
                    break;
                // Summed in compiled order, never in the order inputs finish:
                // float addition is not associative, so this is what makes the
                // parallel executor's output bit-identical to this one.
                for (const auto& input : op.inputs)
                    if (input.valid())
                        out.add(audioIn(input, numSamples));
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

                if (op.inputs[0].valid())
                    audio.copyFrom(audioIn(op.inputs[0], numSamples));
                else
                    audio.clear();

                auto* device = deviceForOp_[i];
                if (device == nullptr)
                    break;

                DeviceBlock deviceBlock{audio, &midiIn(op.inputs[1]), deviceMidiOut, {}, block};
                if (op.inputs[2].valid())
                    deviceBlock.sidechain = audioIn(op.inputs[2], numSamples);
                device->process(deviceBlock);
                break;
            }

            case OpKind::Gain:
            case OpKind::SendTap: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent || !op.inputs[0].valid())
                    out.clear();
                else
                    copyWithGain(out, audioIn(op.inputs[0], numSamples), value, numSamples);
                break;
            }

            case OpKind::Fader: {
                auto out = audioOut(id, 0, numSamples);
                if (value.silent || !op.inputs[0].valid())
                    out.clear();
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
                else
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
