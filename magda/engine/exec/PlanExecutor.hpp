#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <string>
#include <vector>

#include "exec/PlanBindings.hpp"
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

class PlanExecutor {
  public:
    /**
     * @brief Bind a plan to its runtime objects and allocate everything.
     *
     * Off the audio thread. Returns one message per thing the executor cannot
     * honour: an op with no binding, a device asking for latency compensation
     * that does not exist yet, a plan that does not validate. A plan that does
     * not validate is not prepared at all, and process() renders silence until
     * a good one arrives.
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

    void reset();

    const RenderPlan* plan_ = nullptr;
    RenderContext context_;

    // One scratch buffer per output port, allocated here. The buffer
    // assignment pass replaces this with an arena sized from the dependency
    // DAG; until then the mapping is one port, one buffer, which costs memory
    // and nothing else.
    std::vector<juce::AudioBuffer<float>> audioSlots_;
    std::vector<juce::MidiBuffer> midiSlots_;
    /// Events each MIDI port can hold without growing, summed through the MIDI
    /// graph at prepare time. Checked in debug wherever a producer writes.
    std::vector<int> midiEventBounds_;
    std::vector<int> portOffsets_;
    std::vector<int> portSlots_;

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
