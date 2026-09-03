#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "core/TypeIds.hpp"
#include "transport/TempoMap.hpp"
#include "transport/TimeDomains.hpp"

/**
 * @file LaunchHandle.hpp
 * @brief Whether a session slot is playing, and when that changes (#2300).
 *
 * The launcher's state machine, and nothing else. A handle knows that it was
 * asked to start on a beat and has not got there yet; it does not know what a
 * clip is, what a plan is, or what the audio it gates sounds like. What it
 * produces per block is a description of that block, which whatever reads a
 * clip then acts on.
 *
 * The incumbent's is `te::LaunchHandle`, and `SessionClipScheduler`'s own
 * header is explicit that the fork owns the state and MAGDA only sends it
 * commands. Replacing that state is what this is for.
 *
 * Threading is deliberately not decided here. A handle is advanced by one
 * caller and its requests come from another, and how those two meet is
 * #2305's, along with who owns the handle's lifetime. Until then this is a
 * plain object: request, then advance.
 */

namespace magda::engine {

/**
 * @brief Which slot a launch handle belongs to.
 *
 * A track and a scene, because that is what a slot is in the model. The op that
 * renders a session is per track and this is per slot on purpose: an op is a
 * place in the graph and wants to be stable, a handle is state and wants to be
 * per thing that has state.
 */
struct SlotKey {
    TrackId trackId = INVALID_TRACK_ID;
    int sceneIndex = -1;

    bool operator==(const SlotKey&) const = default;

    bool operator<(const SlotKey& other) const {
        return std::tie(trackId, sceneIndex) < std::tie(other.trackId, other.sceneIndex);
    }
};

/**
 * @brief One block, in the domains a handle names things in, and how to cut it.
 *
 * A queued position is named in monotonic beats; where a run began and how far
 * it has got are counted in samples, which is the coordinate no wrap, locate or
 * tempo edit moves (#2336). The timeline faces are here because a piece of a
 * block is reported in them.
 *
 * The map comes with it, because naming an instant inside the block is the one
 * thing a handle does that the ranges cannot answer between them. An instant is
 * a sample and only a sample; every face of it is derived here, through the map,
 * which is what stops two faces of one moment disagreeing (#2330).
 */
struct SyncRange {
    BeatRange timeline;
    BeatRange monotonic;
    SecondsRange seconds;

    /// Where the block sits on the transport's own count.
    SampleRange monotonicSamples;

    /// How many samples the block is, which is what an instant is counted in.
    int numSamples = 0;

    /// What one of those samples is worth. Zero for a block assembled by hand
    /// that did not say, which then converts through its own two seconds ends.
    double sampleRate = 0.0;

    /// The map the block was cut from, or null for a block assembled by hand,
    /// which is then taken to run at one tempo.
    const TempoMap* tempo = nullptr;

    /// What one sample is worth, stated or taken from the block's own faces.
    double rate() const {
        if (sampleRate > 0.0)
            return sampleRate;

        return seconds.length() > 0.0 ? static_cast<double>(numSamples) / seconds.length() : 0.0;
    }

    /**
     * @brief The instant @p sample sounds at.
     *
     * An edge rather than an event: one past the end of the block is a legal
     * answer here, because a range that runs to the end of the block ends at
     * the sample after the last one. An event has to land on a sample the block
     * actually has, which is @ref eventAtMonotonicBeat.
     */
    BlockInstant atSample(int sample) const {
        const auto offset = std::clamp(sample, 0, numSamples);
        return BlockInstant{offset, monotonicSamples.start + SampleDuration{offset}};
    }

    /// The instant the block begins on.
    BlockInstant start() const {
        return atSample(0);
    }

    /**
     * @brief The moment @p at falls on, in the timeline's seconds.
     *
     * Exact rather than nearly so: a block runs at one second per sample rate
     * whatever the tempo is doing, so this is a straight line and not an
     * approximation of a curve.
     */
    double timeAt(BlockInstant at) const {
        if (const auto perSecond = rate(); perSecond > 0.0)
            return seconds.start + (static_cast<double>(at.sample) / perSecond);

        return numSamples > 0
                   ? seconds.start +
                         ((static_cast<double>(at.sample) / static_cast<double>(numSamples)) *
                          seconds.length())
                   : seconds.start;
    }

    /**
     * @brief The timeline beat @p at falls on.
     *
     * The map's, because the block's own two beat ends are a straight line and
     * the map between them is not.
     */
    double timelineBeatAt(BlockInstant at) const {
        if (tempo != nullptr)
            return tempo->timeToBeat(timeAt(at));

        if (numSamples <= 0)
            return timeline.start;

        const auto through = static_cast<double>(at.sample) / static_cast<double>(numSamples);
        return timeline.start + (through * timeline.length());
    }

    /**
     * @brief The monotonic beat @p at falls on.
     *
     * Off the timeline beat, and exact inside a block: what makes the two
     * differ is a wrap, and a wrap ends a block.
     */
    double monotonicBeatAt(BlockInstant at) const {
        return monotonic.start + (timelineBeatAt(at) - timeline.start);
    }

    /**
     * @brief The instant @p beat falls on, as an event this block can carry.
     *
     * Snapped to a sample, and then every face taken from that sample rather
     * than from the beat that asked: a launch happens on a sample or it does
     * not happen, and the faces of the beat it was asked for are the faces of a
     * moment between two samples that nothing plays.
     *
     * Never one past the end, which is where nearest would put a beat in the
     * block's last half sample. That sample belongs to the next callback, and
     * an event placed there is written nowhere: a stop would clear its own note
     * state while its note-offs went to an offset outside the buffer, and the
     * notes would hang. Floor answers 0 to N-1 for anything inside the block
     * without a case to clamp away (TimeDomains::eventAt).
     */
    BlockInstant eventAtMonotonicBeat(double beat) const {
        const auto perSecond = rate();
        if (numSamples <= 0 || seconds.empty() || perSecond <= 0.0)
            return start();

        const auto timelineBeat = timeline.start + (beat - monotonic.start);

        const auto at =
            tempo != nullptr
                ? tempo->beatToTime(timelineBeat)
                : seconds.start + (timeline.empty()
                                       ? 0.0
                                       : ((timelineBeat - timeline.start) / timeline.length()) *
                                             seconds.length());

        return atSample(eventAt((at - seconds.start) * perSecond, numSamples).value);
    }

    /// The part of this block up to @p at, which is the half a run was already
    /// playing when the block ran into something.
    SyncRange upTo(BlockInstant at) const {
        return SyncRange{BeatRange{timeline.start, timelineBeatAt(at)},
                         BeatRange{monotonic.start, monotonicBeatAt(at)},
                         SecondsRange{seconds.start, timeAt(at)},
                         SampleRange{monotonicSamples.start, at.monotonic},
                         at.sample,
                         sampleRate,
                         tempo};
    }

    /// The part from @p at onwards, which is the half whatever happened there
    /// applies to.
    SyncRange from(BlockInstant at) const {
        return SyncRange{BeatRange{timelineBeatAt(at), timeline.end},
                         BeatRange{monotonicBeatAt(at), monotonic.end},
                         SecondsRange{timeAt(at), seconds.end},
                         SampleRange{at.monotonic, monotonicSamples.end},
                         numSamples - at.sample,
                         sampleRate,
                         tempo};
    }
};

/// One piece of a block. A piece sounds exactly when there is a run behind it,
/// so the origin is the whole answer and there is no flag to disagree with it.
struct BlockPiece {
    BeatRange range;

    /// Where the run sounding over this piece began, or nothing. Projected onto
    /// this block, since after a wrap the instant it really started on is not
    /// in this cycle: either face can sit before the block, or before zero.
    std::optional<MaterialOrigin> origin;

    /// Whether the slot sounded over this piece.
    bool playing() const {
        return origin.has_value();
    }
};

/**
 * @brief What one block was, once the handle has been advanced over it.
 *
 * Two pieces rather than one play state: a launch is quantized to a beat and a
 * beat lands wherever it lands inside a block. Answering per block would start
 * every clip on a callback boundary, which is up to 11 ms of slop at 512
 * samples and 120 bpm.
 *
 * The cut is at whatever the block ran into: a launch, a stop, or a loop
 * re-trigger. A block that ran into nothing is one piece.
 */
struct SplitStatus {
    /// The block up to the event, or the whole of it when there was none.
    BlockPiece beforeEvent;

    /// What was left after it. Absent rather than empty, so a caller cannot
    /// read a piece that was never played.
    std::optional<BlockPiece> afterEvent;

    /// Where the cut is, when @ref afterEvent says there was one. The sample
    /// the block ran into its event on, so a caller starts the clip there
    /// rather than working the position out a second time and differently.
    BlockInstant event;

    /// Whether the slot is sounding when the block ends, which is what decides
    /// whether a stop owes note-offs.
    bool playingAtEnd() const {
        return afterEvent ? afterEvent->playing() : beforeEvent.playing();
    }
};

class LaunchHandle {
  public:
    enum class PlayState : char { stopped, playing };

    enum class QueueState : char { stopQueued, playQueued };

    LaunchHandle() = default;

    /// The state as of the last advance.
    PlayState playState() const {
        return playState_;
    }

    /// What has been asked for and has not happened yet, if anything.
    std::optional<QueueState> queuedState() const;

    /// The monotonic beat the queued change is waiting for. Absent when the
    /// change is queued for the next block rather than for a position.
    std::optional<double> queuedPosition() const;

    /**
     * @brief Start playing, at @p monotonicBeat or as soon as possible.
     *
     * Replaces any pending request rather than queueing behind it: two launches
     * arriving before either fires means the second one is what was asked for,
     * the same way two locates leave the cursor where the second one said.
     */
    void play(std::optional<double> monotonicBeat);

    /// Stop playing, at @p monotonicBeat or as soon as possible.
    void stop(std::optional<double> monotonicBeat);

    /**
     * @brief Start as though launched with @p other, optionally delayed.
     *
     * What makes a scene launch one event instead of N: every handle in the
     * scene reports the same played range afterwards, so eight clips launched
     * together stay in phase even if some were already playing.
     */
    void playSynced(const LaunchHandle& other, std::optional<double> monotonicBeat);

    /**
     * @brief Re-trigger the play duration every @p beats.
     *
     * Not the clip's own loop. This restarts the handle as if it had been
     * launched again, which is what resets the played range a source reads its
     * position from; a clip looping inside its own length is the clip's
     * business and happens without the handle knowing. Both exist in the fork
     * and conflating them is right whenever the two lengths agree and wrong
     * whenever they do not.
     *
     * Absent cancels looping.
     */
    void setLooping(std::optional<double> beats);

    /**
     * @brief Move the played position without restarting.
     *
     * Both axes are given rather than one converted from the other: a map says
     * where a beat sits, not how long a run has lasted (#2324). They change
     * units here and they do not become one number.
     */
    void nudge(double beats, SampleDuration samples);

    /**
     * @brief Advance over one block and say what it was.
     *
     * The audio thread's call, once per block, in order. A block passed out of
     * order or skipped puts the played range somewhere the material never was.
     *
     * Once per block and not once per reader: the launcher advances every
     * handle before anything renders (SessionLauncher.hpp), and both of a
     * slot's sources read @ref blockStatus.
     */
    SplitStatus advance(const SyncRange& range);

    /// What the last advance said. Default-constructed until the first one,
    /// which is a slot not rendered yet and therefore not playing.
    const SplitStatus& blockStatus() const {
        return blockStatus_;
    }

    /**
     * @brief Timeline beats this run of playing has covered, unlooped.
     *
     * Monotonically increasing across a loop of the handle, so a source can ask
     * how far in it is without counting wraps itself. Absent while stopped.
     */
    std::optional<BeatRange> playedRange() const;

    /// The same run in monotonic beats, which is what another handle syncs to.
    std::optional<BeatRange> playedMonotonicRange() const;

    /// The run on the transport's own count: where it began and how far it has
    /// got, in the one coordinate a wrap, a locate and a tempo edit all leave
    /// alone. How long it has been going is this divided by the rate, which is
    /// what a source reading a file at its own speed needs (#2336).
    std::optional<SampleRange> playedSampleRange() const;

    /// The last completed run, kept after a stop so a follow action can ask
    /// what it followed.
    std::optional<BeatRange> lastPlayedRange() const;

    /**
     * @brief Blocks in which a loop was too short to be re-triggered fully.
     *
     * A handle reports one event per block, so a loop duration shorter than a
     * block wraps fewer times than it should and the run carries on past where
     * it was due to restart. The alternative is a third sub-range SplitStatus
     * has nowhere to put, and the caller has nowhere to act on.
     *
     * Nothing a session clip can do reaches this: durations are bars and a
     * block is milliseconds. It is counted rather than left silent for the
     * reason TransportClock counts its own loop overflows, which is that a
     * loop that quietly stopped looping is otherwise blamed on the audio
     * device.
     */
    int loopRetriggerOverflows() const {
        return loopRetriggerOverflows_;
    }

  private:
    /**
     * @brief The run in progress: where it began, and how far it has got.
     *
     * One origin rather than one per axis. A run whose faces came from
     * different instants would put a clip's notes and its samples in different
     * places, which is what #2330 closed; keeping the sample is what makes the
     * agreement survive the blocks after the launch as well (#2336).
     */
    struct Run {
        /// Where it began on the transport's count. The durable identity: a
        /// locate does not move it, a wrap does not take it back, a tempo edit
        /// does not rescale it, and two runs that began on the same sample
        /// began together whatever happened to the timeline since.
        SamplePosition origin;

        /// Where it has been advanced to. The far end of the same axis, so
        /// elapsed samples is a subtraction and elapsed seconds is that divided
        /// by the rate, exactly.
        SamplePosition through;

        /// The monotonic beat the origin sample fell on. Not recoverable from
        /// the sample afterwards: a map converts a position, and after a wrap
        /// the origin is not a position on this cycle of the timeline (#2324).
        double originBeat = 0.0;

        /// The timeline beat it fell on, kept for what @ref playedRange
        /// reports. Stale after a wrap by construction, which is why nothing
        /// derives anything from it: the cycle it names has been taken back.
        double originTimelineBeat = 0.0;

        /// Monotonic beats covered since the origin.
        double elapsedBeats = 0.0;

        /**
         * @brief The beat the loop counts from, as it was asked for.
         *
         * Not @ref originBeat, and that is the whole of the retrigger fix. A
         * launch is snapped to the sample it sounds on, and a wrap scheduled
         * from that snapped beat starts the next interval from a rounded value:
         * at 48 kHz and 123 bpm a beat is 23,414.634 samples, so a thousand
         * one-beat retriggers finish about 366 samples late. The schedule
         * counts from the beat that was asked for and is never re-anchored to
         * what any wrap landed on, so the error stays under a sample for ever
         * (#2336).
         */
        double scheduleBeat = 0.0;

        /// The rate the origin was counted at. A block arriving at another rate
        /// re-counts it, so the run keeps the elapsed time it has actually had
        /// rather than silently changing length.
        double sampleRate = 0.0;
    };

    struct Pending {
        QueueState state = QueueState::playQueued;
        std::optional<double> position;

        /// Set only by playSynced: the run to join rather than start. The whole
        /// of it, or the handles agree about the bar and not the sample.
        std::optional<Run> synced;
    };

    /// Begin a run at @p at, scheduled for @p scheduledBeat.
    void beginRun(const SyncRange& range, const BlockInstant& at, double scheduledBeat);

    /// Join @p other's run, as it stands at @p at.
    void joinRun(const SyncRange& range, const BlockInstant& at, const Run& other);

    void endRun();

    /// Carry the current run through @p piece without changing state.
    void extendRun(const SyncRange& piece);

    /// The run's origin in @p piece's own cycle of the timeline, on each axis.
    double virtualStart(const SyncRange& piece) const;
    double virtualStartSeconds(const SyncRange& piece) const;

    /// How long the run had been going when @p piece began.
    double elapsedSecondsAt(const SyncRange& piece) const;

    /// Re-count the origin when the device changes rate under a running slot.
    void followRate(const SyncRange& piece);

    /// Apply whatever the block ran into, at the instant it ran into it.
    void applyEvent(const SyncRange& range, bool fromPending, const BlockInstant& at,
                    double scheduledBeat);

    /// The advance itself, wrapped so @ref blockStatus is stored in one place.
    SplitStatus advanceOver(const SyncRange& range);

    PlayState playState_ = PlayState::stopped;
    std::optional<Pending> pending_;

    std::optional<Run> run_;
    std::optional<BeatRange> lastPlayed_;

    std::optional<double> loopBeats_;

    SplitStatus blockStatus_;

    int loopRetriggerOverflows_ = 0;
};

}  // namespace magda::engine
