#pragma once

#include <optional>
#include <tuple>

#include "core/TypeIds.hpp"
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
 * @brief One block, in the four domains (TimeDomains.hpp).
 *
 * A queued position is named in monotonic beats; how far into its material a
 * run has got is measured in monotonic seconds. Neither is derived from the
 * other (#2324).
 */
struct SyncRange {
    BeatRange timeline;
    BeatRange monotonic;
    SecondsRange seconds;
    SecondsRange monotonicSeconds;
};

/// One instant, in the faces a run's origin is remembered in. Passed together
/// so a run cannot be started with faces of different instants.
struct SyncPoint {
    double timelineBeat = 0.0;
    double monotonicBeat = 0.0;
    double monotonicSeconds = 0.0;
};

/// Where a run began, in the two domains a source reads it against. One value,
/// because a run whose faces came from different instants would put a clip's
/// notes and its samples in different places.
struct RunOrigin {
    double beat = 0.0;
    double seconds = 0.0;

    bool operator==(const RunOrigin&) const = default;
};

/// One piece of a block. A piece sounds exactly when there is a run behind it,
/// so the origin is the whole answer and there is no flag to disagree with it.
struct BlockPiece {
    BeatRange range;

    /// Where the run sounding over this piece began, or nothing. Projected onto
    /// this block from each monotonic domain, since after a wrap the instant it
    /// really started on is not in this cycle: either face can sit before the
    /// block, or before zero.
    std::optional<RunOrigin> origin;

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

    /// Move the played position without restarting. Both faces are given
    /// rather than converted: the caller has the block and therefore has both.
    void nudge(double beats, double seconds);

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

    /// The run in monotonic seconds: how long it has been going, which is what
    /// a source reading a file at its own speed needs.
    std::optional<SecondsRange> playedMonotonicSecondsRange() const;

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
    struct Pending {
        QueueState state = QueueState::playQueued;
        std::optional<double> position;

        /// Set only by playSynced: the run to join rather than start. All
        /// three faces, or the handles agree about the bar and not the sample.
        std::optional<double> syncedTimelineOrigin;
        std::optional<double> syncedMonotonicOrigin;
        std::optional<double> syncedMonotonicSecondsOrigin;
    };

    /// Begin a run at @p at, already @p elapsedBeats and @p elapsedSeconds into
    /// itself. Non-zero only for a synced launch, which joins a run.
    void beginRun(const SyncPoint& at, double elapsedBeats = 0.0, double elapsedSeconds = 0.0);
    void endRun();

    /// Carry the current run through @p piece without changing state.
    void extendRun(const SyncRange& piece);

    /// The run's origin in @p piece's own cycle of the timeline, on each axis.
    double virtualStart(const SyncRange& piece) const;
    double virtualStartSeconds(const SyncRange& piece) const;

    /// Apply whatever the block ran into, at the instant it ran into it.
    void applyEvent(bool fromPending, const SyncPoint& at);

    /// The advance itself, wrapped so @ref blockStatus is stored in one place.
    SplitStatus advanceOver(const SyncRange& range);

    PlayState playState_ = PlayState::stopped;
    std::optional<Pending> pending_;

    std::optional<BeatRange> played_;
    std::optional<BeatRange> playedMonotonic_;
    std::optional<SecondsRange> playedMonotonicSeconds_;
    std::optional<BeatRange> lastPlayed_;

    std::optional<double> loopBeats_;

    SplitStatus blockStatus_;

    int loopRetriggerOverflows_ = 0;
};

}  // namespace magda::engine
