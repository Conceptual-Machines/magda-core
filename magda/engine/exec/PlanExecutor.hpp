#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "exec/PlanBindings.hpp"
#include "exec/PlanLayout.hpp"
#include "exec/PlanValues.hpp"
#include "exec/RenderContext.hpp"
#include "param/ParamResolve.hpp"
#include "plan/PlanDiff.hpp"
#include "plan/RenderPlan.hpp"

/**
 * @file PlanExecutor.hpp
 * @brief The reference executor: one plan, one thread, one op at a time.
 *
 * This is not the engine that ships. The parallel executor is, and it
 * renders the same plans through the same ops; this one exists to be
 * obviously correct and is the arbiter when the two disagree. It stays a
 * straight walk of the op vector with no scheduling: ops are already in
 * dependency order, so running them in order is enough.
 *
 * "Through the same ops" is literal: preparing a plan against its bindings,
 * the buffers it renders through, and the body of every op all live here and
 * both executors use them. The parallel executor only replaces the loop,
 * which is why its output is bit-identical to this one's rather than close
 * to it.
 */

namespace magda::engine {

/**
 * @brief A fixed delay on one audio port, in samples.
 *
 * Reads and writes the same block: the input is captured before the delayed
 * output is read back, so a delay whose output shares its input's buffer
 * behaves no differently from one that doesn't.
 */
class AudioDelayLine {
  public:
    void prepare(int numChannels, int delaySamples, int maxBlockSize);
    void process(juce::dsp::AudioBlock<float> block, int numSamples);

    /// Whether a running line could adopt this configuration rather than be
    /// rebuilt. Everything here changes what the ring means, so any
    /// difference means a fresh start.
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
 * Events move by sample position, and ones falling past the end of a block
 * are held for the next, so storage must cover the whole delay span rather
 * than one block's worth. That span is what kMaxMidiBytesPerPort budgets
 * over -- measured per callback it would vary with the host's block size.
 */
class MidiDelayLine {
  public:
    void prepare(int delaySamples, int capacityBytes);
    void process(const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples);

    /// Whether more has ever been in flight than prepare() reserved room for.
    /// Reported rather than only asserted: past the reservation the buffer
    /// grows on the callback, which a release build would do silently.
    ///
    /// Safe to read while rendering. The per-write asserts only see one
    /// callback's worth (looser than the budget for short callbacks), so
    /// this is where the budget is actually enforced.
    bool hasOverflowed() const {
        return overflowed_.load(std::memory_order_relaxed);
    }

    /// As AudioDelayLine::hasConfiguration. The reservation counts too: a
    /// line whose port now carries more would be adopted with too little room.
    bool hasConfiguration(int delaySamples, int capacityBytes) const {
        return delay_ == delaySamples && capacity_ == capacityBytes;
    }

  private:
    juce::MidiBuffer pending_, scratch_;
    int delay_ = 0;
    /// What prepare() reserved, kept so process() can tell when the
    /// reservation's budget has been exceeded.
    int capacity_ = 0;
    /// Written on the audio thread, read from anywhere. Relaxed both ways:
    /// it orders nothing and guards nothing.
    std::atomic<bool> overflowed_{false};
};

/**
 * @brief How long an edge takes to become the edge that replaced it.
 *
 * Long enough that the step is a slope, short enough that dragging a device
 * around doesn't sound like a fader move. A time rather than a block count,
 * since the executor is block-size invariant everywhere else and a fade
 * lasting longer at 64 samples than at 512 would be the one exception.
 */
inline constexpr double kCrossfadeSeconds = 0.005;

/**
 * @brief The ramp from one side of a changed edge to the other.
 *
 * Equal gain, linearly: the two sides are the same material a moment apart,
 * so they're correlated and sum to full amplitude. A constant-power law
 * (right for uncorrelated material) would bulge in the middle of every
 * device insert.
 *
 * Position is counted off what's left rather than off blocks, so the fade
 * is the same length regardless of callback size, and finishes mid-block
 * rather than waiting for the next boundary.
 */
class CrossfadeRamp {
  public:
    void prepare(int lengthSamples);

    /// Whether a running ramp could adopt this configuration. A different
    /// length is a different fade.
    bool hasConfiguration(int lengthSamples) const {
        return length_ == lengthSamples;
    }

    /// Whether the fade has finished. Safe to read from anywhere, which is
    /// what the thread publishing the next plan asks to know when to let it go.
    bool spent() const {
        return remaining_.load(std::memory_order_relaxed) <= 0;
    }

    /**
     * @brief Write @p numSamples of the fade into @p dest.
     *
     * On the audio thread. @p dest may be the same buffer as @p newSide: every
     * sample is read before the same index is written.
     */
    void process(juce::dsp::AudioBlock<float> dest, juce::dsp::AudioBlock<float> oldSide,
                 juce::dsp::AudioBlock<float> newSide, int numSamples);

  private:
    int length_ = 0;

    /// Samples of fade left. Written on the audio thread, read from the
    /// thread publishing the next plan; relaxed both ways.
    std::atomic<int> remaining_{0};
};

class PlanExecutor {
  public:
    /**
     * @brief Bind a plan to its runtime objects and allocate everything.
     *
     * Off the audio thread. Returns one message per thing the executor
     * can't honour (an unbound op, a plan that fails validation); a plan
     * that fails validation is not prepared at all, and process() renders
     * silence until a good one arrives.
     *
     * @p bindings arrive already prepared for @p context. The executor does
     * not own those objects and must not prepare them itself: they're
     * shared with the epoch still rendering, and preparing a device the
     * audio thread is inside is both a race and a loss of the state a swap
     * exists to keep. Whoever owns them prepares them when made, the one
     * moment nothing can reach them.
     *
     * This is also where everything depending on the bound instances is
     * settled: how many samples each delay holds, and which ports can
     * therefore share a buffer. A plugin changing its reported latency is a
     * re-prepare of the same plan, not a recompile of a new one.
     *
     * @p previous is the executor this one is replacing, if any. Ops the
     * differ matches adopt its state rather than restarting, which keeps a
     * structural edit from cutting off whatever was in flight. Nothing it
     * owns is written to here -- the audio thread is still rendering
     * through it until the swap, so a carried object is shared, never
     * reset, and one that can't be shared is rebuilt beside it instead.
     * @p previous must still be prepared and its plan must still exist,
     * which is why the session holds its own handle on the rendering epoch
     * rather than reaching for whatever is currently published.
     */
    std::vector<std::string> prepare(const RenderPlan& plan, const PlanBindings& bindings,
                                     const RenderContext& context,
                                     const PlanExecutor* previous = nullptr,
                                     const ParamTable* params = nullptr);

    /// Forget the prepared plan and everything sized for it. Off the audio
    /// thread. Every prepare starts here, so a refused plan leaves nothing
    /// behind that could still be rendered.
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
        /// The block as this plan will actually render it: what was asked
        /// for, clipped to what it was prepared for.
        BlockInfo block;

        /// Whether the values belong to this plan. When they don't, every
        /// op renders at unity rather than at whatever the table says.
        bool applyValues = false;

        /// False when there is nothing to render at all: no plan prepared,
        /// or no samples asked for. The output has been cleared either way.
        bool render = false;
    };

    /**
     * @brief Settle what a block is, and clear @p output.
     *
     * Shared with the parallel executor since the two must agree on how
     * much of the block this plan can render and whether the values were
     * resolved against it -- a second copy of that reasoning is a second
     * answer.
     */
    BlockStart beginBlock(const PlanValues& values, const BlockInfo& requested,
                          juce::AudioBuffer<float>& output) const;

    /**
     * @brief Resolve the block's parameters, before any op runs (#2117).
     *
     * On the audio thread, after beginBlock and before anything renders,
     * from both executors: a device reads what its parameters are and the
     * answer has to be there when it does.
     *
     * The table travels with the values, so what makes one applicable makes
     * the other applicable. A table that doesn't belong to this plan leaves
     * every parameter empty rather than resolved against the wrong
     * addresses, handing a device the same window of nothing it would have
     * had before any table was published.
     */
    void resolveParameters(const PlanValues& values, const BlockInfo& block);

    /**
     * @brief Render the MIDI a block has before it has any audio (#2120).
     *
     * On the audio thread, after beginBlock and before resolveParameters,
     * from both executors. Renders every op whose outputs are all MIDI and
     * whose producers are all in that same set -- clip readers, live input
     * queues, the merges that gather them, and the delays on those edges.
     * None of it reads a parameter or audio, so running it early is the
     * same schedule seen from a different place.
     *
     * That buys the one block a note-triggered modifier would otherwise be
     * late by: parameters resolve at the top of a block, so a trigger fired
     * by an op during the walk isn't spent until the next block's resolve,
     * while a trigger read off a MIDI buffer that already exists is spent
     * in this one.
     *
     * MIDI a device makes is not in the prefix and cannot be: an
     * arpeggiator's notes are work that hasn't happened yet at resolve
     * time. A modifier triggered from those is a block late, the same lag
     * an audio trigger has (ModFollower.hpp).
     *
     * The caller must not render these ops again -- they stay in the
     * schedule so their consumers are released as usual, but both executors
     * skip re-running them, since a live input queue rendered twice is a
     * queue read twice.
     */
    void renderMidiPrefix(const PlanValues& values, const BlockInfo& block);

    /// Whether @p op was already rendered by @ref renderMidiPrefix this block.
    bool inMidiPrefix(OpId op) const {
        const auto i = static_cast<std::size_t>(op);
        return i < inMidiPrefix_.size() && inMidiPrefix_[i] != 0;
    }

    /**
     * @brief Render one op of the prepared plan.
     *
     * Every op the engine renders passes through here, from both executors
     * -- what makes their output bit-identical rather than merely close:
     * the arithmetic lives in one place, and scheduling is the only thing
     * the parallel executor changes. Anything that sums does so in compiled
     * order, regardless of what order its inputs arrived in.
     *
     * The caller owes it a schedule: everything this op reads has finished,
     * and nothing that reads what it writes has started; ports share
     * buffers on exactly that promise (see assignBuffers).
     *
     * @p output is touched by Output ops alone. Those accumulate into a
     * buffer every one of them shares, so whoever drives the block runs
     * them itself, in plan order, rather than letting a schedule decide
     * what adds to what first.
     */
    void renderOp(OpId op, const OpValue& value, const BlockInfo& block,
                  juce::AudioBuffer<float>& output);

    /// Meter ops whose tap the bindings filled. The rest still render; they
    /// just publish nothing, since nothing is reading them.
    int boundMeterCount() const {
        return boundMeterCount_;
    }

    /// Value taps this plan's table found a home for (#2122): a parameter
    /// the table carries, or a modifier the runtime holds. A key the
    /// bindings offered that the table doesn't have isn't one of these and
    /// isn't an error -- it's a host asking about a value this project
    /// doesn't move.
    int boundValueTapCount() const {
        return static_cast<int>(paramTaps_.size() + modTaps_.size());
    }

    /**
     * @brief Take back what this plan no longer publishes (#2122).
     *
     * Off the audio thread, only after the swap that made this plan live.
     * Every tap the bindings offered that this table found no home for is
     * cleared, so a host reads no writes at all rather than a value frozen
     * where the last plan left it.
     *
     * Not a rare case: a lane deleted off a fader takes that fader out of
     * the parameter table, since a mixer value is carried only while
     * something reaches it. Without this the tap would sit at the last
     * automated position under a count that never moves again, which the
     * header tells hosts to read as an engine that has stopped.
     *
     * Called after the swap rather than at prepare, since until the swap
     * the epoch being replaced is still rendering -- a tap cleared while
     * still being written would be counting from one again by the next block.
     */
    void clearUnboundValueTaps();

    /// Modulation taps that took over the detector of the executor they
    /// replaced rather than starting again -- the same thing ModRuntime's
    /// carry buys: an edit during playback doesn't restate a gate the
    /// source never changed.
    int carriedTriggerDetectors() const {
        return carriedTriggerDetectors_;
    }

    /// Detectors no two taps share. Every modulation tap owns its own level
    /// and gate, so this is the number of taps: a track read at both its
    /// points has two, since one detector between them would let a level at
    /// one point work the gate at the other.
    int distinctTriggerDetectors() const {
        std::set<const TriggerDetector*> seen;
        for (const auto& detector : triggerForOp_)
            if (detector != nullptr)
                seen.insert(detector.get());
        return static_cast<int>(seen.size());
    }

    /// Samples the prepared plan's output is delayed by: what the devices
    /// along its longest path to the output report between them.
    int latencySamples() const {
        return latencySamples_;
    }

    /// Audio and MIDI buffers the prepared plan renders through. One per
    /// output port would always work; sharing is what the assignment pass
    /// is for, so these are how much it saved.
    int audioBufferCount() const {
        return static_cast<int>(audioSlots_.size());
    }
    int midiBufferCount() const {
        return static_cast<int>(midiSlots_.size());
    }

    /// MIDI delay lines that have held more than they reserved room for --
    /// a producer wrote past the budget every reservation is computed from.
    /// Zero on anything that keeps to it.
    int midiDelayOverflows() const {
        int overflows = 0;
        for (const auto& line : midiDelays_)
            overflows += line->hasOverflowed() ? 1 : 0;
        return overflows;
    }

    /// Delay lines this executor took over from the one it replaced, rather
    /// than building fresh -- what the differ bought.
    int carriedDelayLines() const {
        return carriedDelayLines_;
    }

    /**
     * @brief Per op of the prepared plan: 1 where a fade is still running.
     *
     * Off the audio thread; the whole of the retirement protocol. The pass
     * compiling the next plan asks this: a fade whose edge has arrived is
     * re-emitted while it says 1 and dropped once it doesn't, so a spent
     * fade leaves at the next publish and nothing on the callback ever
     * removes an op from a plan the audio thread can see.
     */
    std::vector<char> unfinishedCrossfades() const;

    /// Fades still running. What unfinishedCrossfades() counts, for tests.
    int activeCrossfades() const;

    /// Fades this executor took over mid-ramp from the one it replaced,
    /// rather than starting again.
    int carriedCrossfades() const {
        return carriedCrossfades_;
    }

    /// Modifiers this executor took over mid-cycle from the one it replaced
    /// -- an LFO that didn't restart because a device was inserted
    /// elsewhere in the project (#2119).
    int carriedModifiers() const {
        return mods_.carried();
    }

    /// True once a valid plan has been prepared.
    bool isPrepared() const {
        return plan_ != nullptr;
    }

    /**
     * @brief The device properties the prepared plan was prepared for.
     *
     * Readable because anything driving the executor has to agree with it,
     * and the disagreement is silent where it matters: process() renders at
     * most maxBlockSize samples and only asserts otherwise, so a caller
     * sizing blocks from a different context prints the first maxBlockSize
     * samples of each one and silence for the rest -- a bounce full of
     * regular gaps, in release, from a build that passed every test.
     */
    const RenderContext& preparedContext() const {
        return context_;
    }

    /**
     * @brief Whether the table @p values carries fits what was prepared for it.
     *
     * The other half of "applicable" -- the half the plan's fingerprint
     * can't answer, since a link edit changes no op and no plan: a table
     * that gained a parameter, moved one, or grew the room a block gathers
     * contributions in still travels on a values publish, matches the
     * plan's fingerprint, and yet doesn't fit what was allocated for it.
     *
     * Three checks, three ways to not fit. The layout says the parameter at
     * an index is still the one that index was resolved for, which a count
     * can't: a macro dropped from one scope and picked up in another leaves
     * the count alone while moving every device window after it, and the
     * windows are cached here from the table the plan was prepared with.
     * The count says the answers are the right size. The link width says a
     * parameter's contributions all fit, since the block gathers them into
     * room found before it started.
     *
     * Asked here so the publisher and the block reach one answer: the
     * publisher escalates such a publish into a structural one, and a block
     * that meets one anyway renders empty rather than stale or half-applied.
     */
    bool fitsParameters(const PlanValues& values) const {
        return values.params == nullptr ||
               (values.params->layoutFingerprint == paramLayout_ &&
                values.params->size() == paramValues_.size() &&
                values.params->maxLinksPerParam <= static_cast<int>(paramScratch_.size()) &&
                values.params->modifierFingerprint == mods_.fingerprint());
    }

    /**
     * @brief Whether process() would apply @p values rather than ignore them.
     *
     * The one definition of "applicable", so a caller deciding which table
     * to hand over and the block that reads it can't reach different
     * answers. A matching fingerprint says the table was resolved against
     * this structure; a matching op count says it's all there, which a
     * table assembled halfway can fail while still carrying the right
     * fingerprint. Anything else renders at unity, which is why whatever
     * publishes an epoch checks this before letting it play, not after.
     */
    bool appliesValues(const PlanValues& values) const {
        return plan_ != nullptr && values.planFingerprint == planFingerprint_ &&
               values.ops.size() == plan_->ops.size();
    }

  private:
    /// Where an op's output port keeps its buffer. Ports are flattened in
    /// op order so a PortRef resolves with two array reads and no branching.
    int slotFor(const PortRef& ref) const {
        return portSlots_[static_cast<std::size_t>(portOffsets_[static_cast<std::size_t>(ref.op)] +
                                                   ref.port)];
    }

    /// One audio slot, as an op sees it. Built from the cached channel
    /// pointers rather than the buffer, since making a block out of a
    /// juce::AudioBuffer writes the buffer's isClear flag: two ops reading
    /// the same port at once would be two threads writing one byte, benign
    /// in value but a data race in fact.
    juce::dsp::AudioBlock<float> audioBlock(std::size_t row, int numSamples) const;

    juce::dsp::AudioBlock<float> audioIn(const PortRef& ref, int numSamples) const;
    juce::dsp::AudioBlock<float> audioOut(OpId op, int port, int numSamples) const;
    const juce::MidiBuffer& midiIn(const PortRef& ref) const;
    juce::MidiBuffer& midiOut(OpId op, int port);

    /// The panic flag beside a MIDI slot (#2418). False for a port the plan
    /// left unconnected, the way midiIn hands back an empty buffer for one.
    bool midiInPanic(const PortRef& ref) const;
    void setMidiOutPanic(OpId op, int port, bool panic);

    /// Whether an op's output port 0 is the buffer one of its inputs
    /// arrived in, so the copy that would fill it has already happened.
    bool writesInPlace(std::size_t op) const {
        return writesInPlace_[op] != 0;
    }

    /// The line an op of this executor's plan drives, for the executor
    /// replacing it to take over. Null where the op drives none.
    const std::shared_ptr<AudioDelayLine>& audioDelayFor(OpId op) const;
    const std::shared_ptr<MidiDelayLine>& midiDelayFor(OpId op) const;
    const std::shared_ptr<CrossfadeRamp>& crossfadeFor(OpId op) const;

    const RenderPlan* plan_ = nullptr;
    RenderContext context_;

    /// The arena. Ports share these where no schedule can want both at
    /// once, worked out in assignBuffers rather than here.
    std::vector<juce::AudioBuffer<float>> audioSlots_;
    std::vector<juce::MidiBuffer> midiSlots_;

    /// All-notes-off beside each MIDI slot, one byte per slot (#2418). Written
    /// by the op that owns the slot, every block, the way its buffer is: there
    /// is no stale flag to clear because nothing reads a slot its producer has
    /// not run.
    std::vector<char> midiSlotPanic_;

    /// Channel pointers into the arena, flattened: slot s, channel c is at
    /// s * numChannels + c, and the row past the last slot is silence_.
    /// Every audio block an op is handed is made from these, so nothing on
    /// the render path touches a juce::AudioBuffer (see audioBlock).
    std::vector<float*> slotChannels_;
    /// Encoded bytes each MIDI buffer can hold without growing, summed
    /// through the MIDI graph at prepare time. Checked in debug where
    /// producers write.
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
    /// only one epoch renders at a time, so there's an owner to outlive but
    /// no concurrent use to guard against.
    std::vector<int> audioDelayForOp_;
    std::vector<int> midiDelayForOp_;
    std::vector<std::shared_ptr<AudioDelayLine>> audioDelays_;
    std::vector<std::shared_ptr<MidiDelayLine>> midiDelays_;
    int latencySamples_ = 0;
    int carriedDelayLines_ = 0;

    /// Per op: the ramp a Crossfade op runs, or -1. Held by shared pointer
    /// for the same reason the delay lines are: a fade the next plan
    /// re-emits is still running while the audio thread is inside the
    /// executor that owns it, so the one taking over shares it rather than
    /// copying where it had got to. A fade whose two sides don't arrive at
    /// the same latency has no ramp at all and passes the new side through
    /// (see prepare).
    std::vector<int> crossfadeForOp_;
    std::vector<std::shared_ptr<CrossfadeRamp>> crossfades_;
    int carriedCrossfades_ = 0;

    /// Identity of the prepared plan; values not carrying the same one were
    /// resolved against something else and are not applied.
    std::uint64_t planFingerprint_ = 0;

    // Bindings resolved per op, so the audio thread never hashes anything.
    std::vector<EngineDevice*> deviceForOp_;

    /// The outside world behind each InsertSend and InsertReturn op
    /// (#2245). Both halves of one insert hold the same pointer, since a
    /// round trip is one object.
    std::vector<EngineInsert*> insertForOp_;
    std::vector<EngineAudioSource*> audioSourceForOp_;
    std::vector<EngineMidiSource*> midiSourceForOp_;

    /// Whether each op's track was inaudible last block, for the edge a device
    /// panics on (#2418). Only the op's own thread touches its entry, so the
    /// parallel executor reads and writes it the same way.
    std::vector<char> wasInaudible_;

    /// Handed to ops whose input slot the plan left unconnected. Kept
    /// zeroed: nothing writes to it.
    juce::AudioBuffer<float> silence_;
    juce::MidiBuffer noMidi_;

    /// Per op: where a Meter op publishes, or null for one nobody reads.
    /// Bound once here so the audio thread never looks a key up.
    std::vector<LevelTap*> meterForOp_;
    int boundMeterCount_ = 0;

    /// Where a MergeMidi op publishes what reached it, or nullptr. See
    /// PlanBindings::midiTaps.
    std::vector<MidiTap*> midiTapForOp_;

    /** @brief One value the block publishes for whoever is drawing it (#2122). */
    struct BoundValueTap {
        /// A ParamId, or an index into the modifier runtime.
        int index = 0;
        ValueTap* tap = nullptr;
    };

    /// Compact rather than one slot per parameter, the difference between
    /// these and the per-op bindings above: a plan has hundreds of ops and
    /// a host binds a good fraction of the meters, while a table has a
    /// parameter per parameter of every device in the project and a host
    /// draws only the few dozen on screen. A vector the length of the table
    /// would be thousands of nulls walked every block to find the few bound
    /// ones.
    ///
    /// Resolved at prepare against the table the plan was prepared with, so
    /// the block never looks a key up, and safe to hold by index for
    /// exactly as long as the windows are -- a table whose layout differs
    /// is refused whole (see fitsParameters).
    std::vector<BoundValueTap> paramTaps_;
    std::vector<BoundValueTap> modTaps_;

    /// The other side of those two: taps the bindings offered that this
    /// table has nowhere to publish from. Kept so the swap can clear them,
    /// the only thing anything does with them.
    std::vector<ValueTap*> unboundTaps_;

    /// This block's parameter values, and the room the resolver gathers one
    /// parameter's links in. Sized at prepare from the table the plan was
    /// published with, so the block filling them allocates nothing.
    ResolvedParams paramValues_;
    std::vector<ModContribution> paramScratch_;

    /// Where the modifiers of this plan have got to (#2119). Sized at
    /// prepare from the same table, carried from the executor being
    /// replaced, so a device insert doesn't restart every LFO in the project.
    ModRuntime mods_;

    /**
     * @brief Ops that can run before the block's parameters are resolved.
     *
     * Every op whose outputs are all MIDI and whose producers are all in
     * this set: clip readers, live input queues, the merges that gather
     * them, and the delays on those edges -- the MIDI a block has before
     * any audio exists. None of it reads a parameter or audio, so running
     * it first is the same schedule seen from a different place, not a
     * reordering.
     *
     * That buys the one block a note-triggered modifier would otherwise be
     * late by: parameters resolve at the top of a block, so a trigger fired
     * by an op isn't spent until the next block's resolve, while a trigger
     * read off a MIDI buffer that already exists is spent in this one. The
     * fork's own gate is the later of the two and its monitor the earlier,
     * so this makes the fork's best case unconditional.
     *
     * MIDI a device makes is not in here and cannot be: an arpeggiator's
     * notes are audio-thread work that hasn't happened yet at resolve time.
     * A modifier triggered from those is a block late, the same lag an
     * audio trigger has, documented in the same place.
     */
    std::vector<OpId> midiPrefix_;

    /// Per op: whether it belongs to @ref midiPrefix_, so the block's main
    /// walk leaves alone what the prefix already rendered.
    std::vector<char> inMidiPrefix_;

    /// Per op: the modifiers a ModSource op feeds, or an empty span.
    /// Resolved at prepare from the table's own record of what listens to what.
    std::vector<std::span<const int>> modSourceForOp_;

    /// One block of the source's audio, downmixed to mono, which is what a
    /// detector reads. Sized at prepare.
    std::vector<float> detectMono_;

    /// Per follower: what its own detection needs to know about the level
    /// its audio trigger is at, kept out of the modifier states since it
    /// belongs to the source rather than the modifier.
    struct TriggerDetector {
        float envelope = 0.0f;
        bool open = false;
    };

    /// Per ModSource op, indexed the same way the ops are. Held by shared
    /// pointer for the reason the delay lines are: a tap the next plan also
    /// has is the same detector rather than a copy read at some instant,
    /// and only one epoch renders at a time so there's an owner to outlive
    /// and no concurrent use to guard against.
    std::vector<std::shared_ptr<TriggerDetector>> triggerForOp_;

    int carriedTriggerDetectors_ = 0;

    /**
     * @brief The table this block resolved against, or null.
     *
     * Set once at the top of the block and read by ops that need to name a
     * modifier. Not a second copy of anything: it's the same table
     * resolveParameters was handed, kept for the length of the walk because
     * a modulation tap runs inside the walk and the values it was given
     * don't reach renderOp.
     */
    const ParamTable* blockTable_ = nullptr;

    /// Settle @ref blockTable_ for this block. Called by whichever of the
    /// two entry points runs first, and idempotent, since beginBlock is
    /// const and the prefix needs the answer before the resolve does.
    void settleBlockTable(const PlanValues& values);

    /// Read one modulation tap: hand the source's level to the followers
    /// listening to it, and its rises and falls to the triggered ones.
    void renderModSource(OpId id, const BlockInfo& block);

    /**
     * @brief Publish what this block settled, to whoever is watching (#2122).
     *
     * On the audio thread, at the end of the resolve and nowhere else --
     * the moment both numbers exist and the last moment they're still the
     * block's own rather than something a device has since been handed.
     *
     * A block that resolved no table publishes nothing at all, rather than
     * the zeros an empty table would answer with. The values a host is
     * drawing then hold, as they do whenever nothing is rendering --
     * slamming every watched knob to zero for the one block a mismatched
     * table takes to be replaced would be a visible fault reporting an
     * invisible one.
     */
    void publishValueTaps();

    /// Spend the notes in @p midi on the modifiers listening to @p op's track.
    void feedNoteTriggers(std::size_t op, const juce::MidiBuffer& midi);

    /** @brief The parameters a mixer op reads instead of the published value. */
    struct OpMixerParams {
        /// A fader's volume or a send's level, in dB. Invalid where nothing
        /// reaches the value and the published one stands.
        ParamId gain = INVALID_PARAM_ID;

        /// A fader's pan. Invalid on a send, which has none.
        ParamId pan = INVALID_PARAM_ID;
    };

    /// Per op, resolved at prepare so the audio thread never looks a key up.
    std::vector<OpMixerParams> mixerParamForOp_;

    /// The gains @p op renders with: what the publisher resolved, unless a
    /// lane or a link reaches the value it stands for, in which case what
    /// the block resolved wins. A fader is the one value both layers can
    /// have an opinion about, and the block's is the newer one by exactly
    /// the amount a lane moves inside it.
    OpValue mixerValueFor(std::size_t op, const OpValue& published) const;

    /// Where one parameter's curve is baked before it is resolved. One
    /// parameter's worth: the block walks them one at a time, and a segment
    /// written for one is read before the next is baked.
    std::vector<ParamSegment> paramSegments_;

    /// Per op: the window of the table a Device op's device reads, resolved
    /// once here so the audio thread never hashes a DeviceKey. Cached from
    /// one table's layout, which is what paramLayout_ below is for.
    std::vector<ParamTable::DeviceWindow> paramWindowForOp_;

    /// Identity of the layout those windows were cached from. A table with
    /// another one puts different parameters at the same indices, and the
    /// windows would hand a device its neighbour's slots.
    std::uint64_t paramLayout_ = 0;
};

}  // namespace magda::engine
