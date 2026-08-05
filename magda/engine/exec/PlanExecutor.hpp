#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "exec/PlanBindings.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderContext.hpp"
#include "plan/PlanDiff.hpp"
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

    /// Whether a line already running is one this configuration could adopt
    /// rather than build. Everything here changes what the ring means, so
    /// anything that differs is a new line and a fresh start.
    bool hasConfiguration(int numChannels, int delaySamples, int maxBlockSize) const {
        return delay_ == delaySamples && ring_.getNumChannels() == numChannels &&
               ring_.getNumSamples() == delaySamples + maxBlockSize;
    }

  private:
    juce::AudioBuffer<float> ring_;
    int delay_ = 0;
    int writePosition_ = 0;
};

/**
 * @brief The same delay for one MIDI port.
 *
 * Events move by sample position and the ones that fall past the end of the
 * block are held for the next one, so the storage has to cover every sample the
 * delay spans rather than one block's worth. That span is what
 * kMaxMidiBytesPerPort is a budget over: measured per callback it would be
 * whatever the host chose the block size to be.
 */
class MidiDelayLine {
  public:
    void prepare(int delaySamples, int capacityBytes);
    void process(const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples);

    /// Whether more has ever been in flight than prepare() reserved room for.
    /// Reported rather than only asserted: past the reservation the buffer
    /// grows on the callback, and a release build would do that quietly.
    ///
    /// Safe to read while rendering. The per-write asserts can only see one
    /// callback's worth, which for short callbacks is looser than the budget
    /// the reservation is computed from, so this is where the budget is
    /// actually enforced and it has to be readable from wherever asks.
    bool hasOverflowed() const {
        return overflowed_.load(std::memory_order_relaxed);
    }

    /// As AudioDelayLine::hasConfiguration. The reservation is part of it: a
    /// line whose port now carries more would be adopted with too little room.
    bool hasConfiguration(int delaySamples, int capacityBytes) const {
        return delay_ == delaySamples && capacity_ == capacityBytes;
    }

  private:
    juce::MidiBuffer pending_, scratch_;
    int delay_ = 0;
    /// What prepare() reserved, kept so process() can tell when the budget the
    /// reservation was computed from has been exceeded.
    int capacity_ = 0;
    /// Written on the audio thread, read from anywhere. Relaxed both ways: it
    /// orders nothing and guards nothing, it only has to be a read and a write
    /// the standard has an answer for.
    std::atomic<bool> overflowed_{false};
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
     * @p bindings arrive already prepared for @p context. The executor does not
     * own those objects and must not prepare them: they are shared with the
     * epoch still rendering, and preparing a device the audio thread is inside
     * is both a race and the loss of the state a swap exists to keep. Whoever
     * owns them prepares them when they are made, which is the one moment
     * nothing can reach them.
     *
     * This is also where everything that depends on the bound instances is
     * settled: how many samples each delay holds, and therefore which ports can
     * share a buffer. A plugin that changes its reported latency is a re-prepare
     * of the same plan, not a recompile of a new one.
     *
     * @p previous is the executor this one is about to replace, if there is
     * one. Ops the differ matches adopt its state rather than starting again,
     * which is what keeps a structural edit from cutting whatever was in
     * flight. Nothing it owns is written to here: the audio thread is still
     * rendering through it until the swap, so a carried object is shared, never
     * reset, and one that cannot be shared is rebuilt beside it instead.
     *
     * It has to still be prepared, and the plan it was prepared with has to
     * still exist, which is why the session holds its own handle on the epoch
     * that is rendering rather than reaching for whatever is published.
     */
    std::vector<std::string> prepare(const RenderPlan& plan, const PlanBindings& bindings,
                                     const RenderContext& context,
                                     const PlanExecutor* previous = nullptr);

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

    /// MIDI delay lines that have held more than they reserved room for, which
    /// means a producer wrote past the budget every reservation here is
    /// computed from. Zero on anything that keeps to it.
    int midiDelayOverflows() const {
        int overflows = 0;
        for (const auto& line : midiDelays_)
            overflows += line->hasOverflowed() ? 1 : 0;
        return overflows;
    }

    /// Delay lines this executor took over from the one it replaced, rather
    /// than building. What the differ bought, in other words.
    int carriedDelayLines() const {
        return carriedDelayLines_;
    }

    /// True once a valid plan has been prepared.
    bool isPrepared() const {
        return plan_ != nullptr;
    }

    /**
     * @brief Whether process() would apply @p values rather than ignore them.
     *
     * The one definition of applicable, so that a caller deciding which table
     * to hand over and the block that reads it cannot come to different
     * answers. A matching fingerprint says the table was resolved against this
     * structure; a matching op count says it is all there, which a table
     * assembled halfway can fail while still carrying the right fingerprint.
     * Anything else renders at unity, which is why what publishes an epoch
     * checks this before letting it play rather than after.
     */
    bool appliesValues(const PlanValues& values) const {
        return plan_ != nullptr && values.planFingerprint == planFingerprint_ &&
               values.ops.size() == plan_->ops.size();
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

    /// The line an op of this executor's plan drives, for the executor
    /// replacing it to take over. Null where the op drives none.
    const std::shared_ptr<AudioDelayLine>& audioDelayFor(OpId op) const;
    const std::shared_ptr<MidiDelayLine>& midiDelayFor(OpId op) const;

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
    ///
    /// Held by shared pointer because a line can outlive the executor that
    /// built it: the one replacing this takes a reference to every line it
    /// adopts, and the rest go when this does. Nothing else shares them, and
    /// only one epoch renders at a time, so there is no concurrent use to
    /// guard against, only an owner to outlive.
    std::vector<int> audioDelayForOp_;
    std::vector<int> midiDelayForOp_;
    std::vector<std::shared_ptr<AudioDelayLine>> audioDelays_;
    std::vector<std::shared_ptr<MidiDelayLine>> midiDelays_;
    int latencySamples_ = 0;
    int carriedDelayLines_ = 0;

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
