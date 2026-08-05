#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <string>
#include <vector>

#include "exec/PlanBindings.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderContext.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file PlanExecutor.hpp
 * @brief The reference executor: one plan, one thread, one op at a time.
 *
 * This is not the engine that ships. The parallel executor is, and it renders
 * the same plans through the same ops; this one exists to be obviously correct.
 * When the two disagree, this is the arbiter, so it stays a straight walk of
 * the op vector with no scheduling of any kind: ops are in dependency order, so
 * running them in order is enough.
 */

namespace magda::engine {

/**
 * @brief A fixed delay on one audio port, in samples.
 *
 * Reads and writes the same block: the input is captured before the delayed
 * output is read back, so a delay whose output shares its input's buffer is no
 * different from one that does not.
 */
class AudioDelayLine {
  public:
    void prepare(int numChannels, int delaySamples, int maxBlockSize);
    void process(juce::dsp::AudioBlock<float> block, int numSamples);

  private:
    juce::AudioBuffer<float> ring_;
    int delay_ = 0;
    int writePosition_ = 0;
};

/**
 * @brief The same delay for one MIDI port.
 *
 * Events move by sample position and the ones that fall past the end of the
 * block are held for the next one, so the storage has to cover every block the
 * delay spans rather than one block's worth.
 */
class MidiDelayLine {
  public:
    void prepare(int delaySamples, int capacityBytes);
    void process(const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples);

  private:
    juce::MidiBuffer pending_, scratch_;
    int delay_ = 0;
};

class PlanExecutor {
  public:
    /**
     * @brief Bind a plan to its runtime objects and allocate everything.
     *
     * Off the audio thread. Returns one message per thing the executor cannot
     * honour: an op with no binding, a plan that does not validate. A plan that
     * does not validate is not prepared at all, and process() renders silence
     * until a good one arrives.
     *
     * This is also where everything that depends on the bound instances is
     * settled: how many samples each delay holds, and therefore which ports can
     * share a buffer. A plugin that changes its reported latency is a re-prepare
     * of the same plan, not a recompile of a new one.
     */
    std::vector<std::string> prepare(const RenderPlan& plan, const PlanBindings& bindings,
                                     const RenderContext& context);

    /**
     * @brief Render one block into @p output.
     *
     * Audio-thread code: no allocation, no locks, no logging. @p values must
     * have been resolved against the prepared plan; anything else is ignored
     * and the block renders at unity.
     */
    void process(const PlanValues& values, const BlockInfo& block,
                 juce::AudioBuffer<float>& output);

    /// Peak level of each Meter op's block, indexed by OpId, zero elsewhere.
    /// A plain vector for now: publishing meters to the UI thread without
    /// tearing is the metering slice's problem, not the executor's.
    const std::vector<float>& meterLevels() const {
        return meterLevels_;
    }

    /// Samples the prepared plan's output is delayed by: what the devices along
    /// its longest path to the output report between them.
    int latencySamples() const {
        return latencySamples_;
    }

    /// Audio and MIDI buffers the prepared plan renders through. One per output
    /// port would always work; sharing is what the assignment pass is for, so
    /// these are how much it saved.
    int audioBufferCount() const {
        return static_cast<int>(audioSlots_.size());
    }
    int midiBufferCount() const {
        return static_cast<int>(midiSlots_.size());
    }

    /// True once a valid plan has been prepared.
    bool isPrepared() const {
        return plan_ != nullptr;
    }

  private:
    /// Where an op's output port keeps its buffer. Ports are flattened in op
    /// order so a PortRef resolves with two array reads and no branching.
    int slotFor(const PortRef& ref) const {
        return portSlots_[static_cast<std::size_t>(portOffsets_[static_cast<std::size_t>(ref.op)] +
                                                   ref.port)];
    }

    juce::dsp::AudioBlock<float> audioIn(const PortRef& ref, int numSamples);
    juce::dsp::AudioBlock<float> audioOut(OpId op, int port, int numSamples);
    const juce::MidiBuffer& midiIn(const PortRef& ref) const;
    juce::MidiBuffer& midiOut(OpId op, int port);

    /// Whether an op's output port 0 is the buffer one of its inputs arrived
    /// in, so the copy that would fill it has already happened.
    bool writesInPlace(std::size_t op) const {
        return writesInPlace_[op] != 0;
    }

    void reset();

    const RenderPlan* plan_ = nullptr;
    RenderContext context_;

    /// The arena. Ports share these where no schedule can want both at once,
    /// which is worked out in assignBuffers rather than here.
    std::vector<juce::AudioBuffer<float>> audioSlots_;
    std::vector<juce::MidiBuffer> midiSlots_;
    /// Encoded bytes each MIDI buffer can hold without growing, summed through
    /// the MIDI graph at prepare time. Checked in debug where producers write.
    std::vector<int> midiByteBounds_;
    std::vector<int> portOffsets_;
    std::vector<int> portSlots_;
    std::vector<char> writesInPlace_;

    /// Per op: the delay line it drives, or -1. Ops that need none are the
    /// common case, and a plan with no latency in it allocates nothing here.
    std::vector<int> audioDelayForOp_;
    std::vector<int> midiDelayForOp_;
    std::vector<AudioDelayLine> audioDelays_;
    std::vector<MidiDelayLine> midiDelays_;
    int latencySamples_ = 0;

    /// Identity of the prepared plan; values that do not carry the same one
    /// were resolved against something else and are not applied.
    std::uint64_t planFingerprint_ = 0;

    // Bindings resolved per op, so the audio thread never hashes anything.
    std::vector<EngineDevice*> deviceForOp_;
    std::vector<EngineAudioSource*> audioSourceForOp_;
    std::vector<EngineMidiSource*> midiSourceForOp_;

    /// Handed to ops whose input slot the plan left unconnected. Kept zeroed:
    /// nothing writes to it.
    juce::AudioBuffer<float> silence_;
    juce::MidiBuffer noMidi_;

    std::vector<float> meterLevels_;
};

}  // namespace magda::engine
