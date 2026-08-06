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
 *
 * "Through the same ops" is meant literally. Everything below the walk lives
 * here and both executors use it: preparing a plan against its bindings, the
 * buffers it renders through, and the body of every op. What the parallel
 * executor replaces is the loop and nothing else, which is why its output is
 * bit-identical to this one's rather than close to it.
 */

namespace magda::engine {

/**
 * @brief How long a fade on a changed edge lasts.
 *
 * Long enough to take the corner off a step and short enough that nobody hears
 * the two signals as separate: past about ten milliseconds an insertion starts
 * to sound like a dissolve rather than an edit, and under about two the ramp is
 * steep enough to click on its own.
 */
constexpr double kCrossfadeSeconds = 0.005;

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

/**
 * @brief The fade on one edge a structural edit changed, in samples.
 *
 * Equal gain rather than constant power, because the two sides are the same
 * material a moment apart and not two unrelated signals: correlated inputs sum
 * to full amplitude, so a power law would bulge in the middle where a linear one
 * holds level.
 *
 * Spent is the resting state, not a state to be cleaned up. Once the ramp has
 * run its length the op is a pass-through of the new side, which costs a copy
 * the buffer assignment usually elides anyway, and the plan is rid of it at the
 * next structural edit.
 */
class CrossfadeRamp {
  public:
    void prepare(int lengthSamples) {
        length_ = lengthSamples;
        remaining_.store(lengthSamples, std::memory_order_relaxed);
    }

    /// As AudioDelayLine::hasConfiguration. A fade of a different length is a
    /// different fade, and resuming one part-way through at the wrong rate would
    /// finish early or hang at the end.
    bool hasConfiguration(int lengthSamples) const {
        return length_ == lengthSamples;
    }

    /// Whether the ramp has run its length. Readable from anywhere, because
    /// the pass that builds the next plan asks it: a fade still running has to
    /// be re-emitted so it finishes, and one that has finished has to be left
    /// out so the plan is rid of it.
    bool spent() const {
        return remaining_.load(std::memory_order_relaxed) <= 0;
    }

    /**
     * @brief Fade @p oldSide into @p newSide over @p numSamples.
     *
     * @p destination may be @p newSide's own buffer, which is what the buffer
     * assignment arranges wherever it can: every sample is read before the one
     * at the same index is written. It is never @p oldSide's, because the fade
     * is a reader of that port and a slot is only reused for an op that
     * everything reading it has finished before.
     *
     * Past the end of the ramp the new side is copied through, so a block
     * straddling the end of a fade is not a special case for the caller.
     */
    void process(juce::dsp::AudioBlock<float> destination, juce::dsp::AudioBlock<float> oldSide,
                 juce::dsp::AudioBlock<float> newSide, int numSamples);

  private:
    int length_ = 0;
    /// Written on the audio thread, read from anywhere. Relaxed both ways: it
    /// orders nothing and guards nothing, it only has to be a read and a write
    /// the standard has an answer for. Same arrangement as
    /// MidiDelayLine::overflowed_.
    std::atomic<int> remaining_{0};
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

    /// Forget the prepared plan and everything sized for it. Off the audio
    /// thread. Every prepare starts here, so a plan that is refused leaves
    /// nothing behind that could still be rendered.
    void reset();

    /**
     * @brief Render one block into @p output.
     *
     * Audio-thread code: no allocation, no locks, no logging. @p values must
     * have been resolved against the prepared plan; anything else is ignored
     * and the block renders at unity.
     */
    void process(const PlanValues& values, const BlockInfo& block,
                 juce::AudioBuffer<float>& output);

    /** @brief Where one block's render starts, before any op has run. */
    struct BlockStart {
        /// The block as this plan will actually render it, which is the one
        /// asked for clipped to what it was prepared for.
        BlockInfo block;

        /// Whether the values belong to this plan. When they do not, every op
        /// renders at unity rather than at whatever the table happens to say.
        bool applyValues = false;

        /// False when there is nothing to render at all: no plan prepared, or
        /// no samples asked for. The output has been cleared either way.
        bool render = false;
    };

    /**
     * @brief Settle what a block is, and clear @p output.
     *
     * Shared with the parallel executor because the two have to agree on it:
     * how much of the block this plan can render, and whether the values were
     * resolved against it. A second copy of that reasoning is a second answer.
     */
    BlockStart beginBlock(const PlanValues& values, const BlockInfo& requested,
                          juce::AudioBuffer<float>& output) const;

    /**
     * @brief Render one op of the prepared plan.
     *
     * Every op the engine renders passes through here, from both executors,
     * and that is what makes their output bit-identical rather than merely
     * close: the arithmetic is in one place, and scheduling is the only thing
     * the parallel executor changes. Anything that sums does so in compiled
     * order, whatever order its inputs arrived in.
     *
     * The caller owes it a schedule. Every op this one reads has finished, and
     * nothing that reads what it writes has started; ports share buffers on
     * exactly that promise (see assignBuffers).
     *
     * @p output is touched by Output ops alone. Those accumulate into a buffer
     * every one of them shares, so whoever drives the block runs them itself,
     * in plan order, rather than letting a schedule decide what adds to what
     * first.
     */
    void renderOp(OpId op, const OpValue& value, const BlockInfo& block,
                  juce::AudioBuffer<float>& output);

    /// Meter ops whose tap the bindings filled. The rest still render; they
    /// publish nothing, because nothing is reading them.
    int boundMeterCount() const {
        return boundMeterCount_;
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

    /// Fade ops in the prepared plan that are actually fading. The rest render
    /// as pass-throughs: see prepare for the one thing that turns a fade off.
    int activeCrossfades() const {
        return activeCrossfades_;
    }

    /// Fades this executor resumed from the one it replaced rather than
    /// restarting. A re-prepare of the same plan is the case that needs it: the
    /// fade is mid-flight and a plugin reporting a new latency is not a reason
    /// to start it again.
    int carriedCrossfades() const {
        return carriedCrossfades_;
    }

    /**
     * @brief Per op of the prepared plan: whether a fade there is still running.
     *
     * What insertCrossfades needs from the epoch it is building a successor
     * for, and the reason a fade retires without a plan of its own. An edit
     * arriving while a fade is in flight re-emits it and the ramp carries; one
     * arriving after it has finished leaves it out, and the plan is back to what
     * the compiler produced. Zero for everything that is not a fade.
     *
     * On the publishing thread, reading state the audio thread writes. Relaxed:
     * a fade one block from the end that reads as unfinished is re-emitted and
     * finishes in the plan that replaces it, which is the same answer either way.
     */
    std::vector<char> unfinishedCrossfades() const;

    /// True once a valid plan has been prepared.
    bool isPrepared() const {
        return plan_ != nullptr;
    }

    /**
     * @brief The device properties the prepared plan was prepared for.
     *
     * Readable because anything driving the executor has to agree with it, and
     * because the disagreement is silent where it matters. process() renders at
     * most maxBlockSize samples and says so only through an assert, so a caller
     * that sized its blocks from a different context prints the first
     * maxBlockSize samples of each one and silence for the rest: a bounce full
     * of regular gaps, in release, from a build that passed every test.
     */
    const RenderContext& preparedContext() const {
        return context_;
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

    /// One audio slot, as an op sees it. Built from the cached channel
    /// pointers rather than from the buffer, because making a block out of a
    /// juce::AudioBuffer writes the buffer's isClear flag: two ops reading the
    /// same port at once would be two threads writing one byte, which is
    /// benign in value and a data race in fact.
    juce::dsp::AudioBlock<float> audioBlock(std::size_t row, int numSamples) const;

    juce::dsp::AudioBlock<float> audioIn(const PortRef& ref, int numSamples) const;
    juce::dsp::AudioBlock<float> audioOut(OpId op, int port, int numSamples) const;
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

    /// The ramp an op of this executor's plan is running, for the executor
    /// replacing it to take over. Null where the op is not fading.
    const std::shared_ptr<CrossfadeRamp>& crossfadeFor(OpId op) const;

    const RenderPlan* plan_ = nullptr;
    RenderContext context_;

    /// The arena. Ports share these where no schedule can want both at once,
    /// which is worked out in assignBuffers rather than here.
    std::vector<juce::AudioBuffer<float>> audioSlots_;
    std::vector<juce::MidiBuffer> midiSlots_;

    /// Channel pointers into the arena, flattened: slot s, channel c is at
    /// s * numChannels + c, and the row past the last slot is silence_. Every
    /// audio block an op is handed is made from these, so nothing on the
    /// render path touches a juce::AudioBuffer (see audioBlock).
    std::vector<float*> slotChannels_;
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

    /// Per op: the fade it is running, or -1. Held by shared pointer for the
    /// same reason a delay line is: the executor replacing this one takes a
    /// reference to every ramp it resumes, and a ramp mid-flight is state the
    /// swap exists to keep.
    std::vector<int> crossfadeForOp_;
    std::vector<std::shared_ptr<CrossfadeRamp>> crossfades_;
    int activeCrossfades_ = 0;
    int carriedCrossfades_ = 0;

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

    /// Per op: where a Meter op publishes, or null for one nobody reads. Bound
    /// once here so the audio thread never looks a key up.
    std::vector<LevelTap*> meterForOp_;
    int boundMeterCount_ = 0;
};

}  // namespace magda::engine
