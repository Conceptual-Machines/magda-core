#pragma once

#include <optional>

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

/// A stretch of beats. Half open: a block ending at 4.0 and one starting there
/// do not both contain beat 4.
struct BeatRange {
    double start = 0.0;
    double end = 0.0;

    double length() const {
        return end - start;
    }

    bool empty() const {
        return end <= start;
    }

    bool operator==(const BeatRange&) const = default;
};

/**
 * @brief One block, as the launcher sees it.
 *
 * Two faces of the same stretch. The timeline range is where the material is
 * and goes backwards when the loop wraps; the monotonic range only ever moves
 * forwards and is what a queued position is named in. Both come from the same
 * BlockInfo, which is where they were derived together.
 */
struct SyncRange {
    BeatRange timeline;
    BeatRange monotonic;
};

/**
 * @brief What one block was, once the handle has been advanced over it.
 *
 * Two sub-ranges rather than one play state, because a launch is quantized to
 * a beat and a beat lands wherever it lands inside a block. A handle that
 * answered per block would start every clip on a block boundary: at 512
 * samples and 120 bpm that is up to 11 ms of slop, it differs per track, and
 * removing exactly that slop is what quantized launch is for.
 *
 * When @ref isSplit is false only the first half is meaningful and it covers
 * the whole block.
 */
struct SplitStatus {
    bool playing1 = false;
    bool playing2 = false;

    BeatRange range1;
    BeatRange range2;

    bool isSplit = false;

    /// The timeline beat the current run of playing began at, for whichever
    /// sub-ranges are playing. What a source subtracts to know how far into
    /// the clip it is.
    std::optional<double> playStartTime1;
    std::optional<double> playStartTime2;
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

    /// Move the played position by @p beats without restarting.
    void nudge(double beats);

    /**
     * @brief Advance over one block and say what it was.
     *
     * The audio thread's call, once per block, in order. A block passed out of
     * order or skipped puts the played range somewhere the material never was.
     */
    SplitStatus advance(const SyncRange& range);

    /**
     * @brief Timeline beats this run of playing has covered, unlooped.
     *
     * Monotonically increasing across a loop of the handle, so a source can ask
     * how far in it is without counting wraps itself. Absent while stopped.
     */
    std::optional<BeatRange> playedRange() const;

    /// The same run in monotonic beats, which is what another handle syncs to.
    std::optional<BeatRange> playedMonotonicRange() const;

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

        /// Set only by playSynced: the run to join rather than start.
        std::optional<double> syncedTimelineOrigin;
        std::optional<double> syncedMonotonicOrigin;
    };

    /// Begin a run whose origin is @p timelineBeat and @p monotonicBeat, and
    /// which is already @p elapsed beats into itself. Non-zero only for a
    /// synced launch, which joins a run rather than starting one.
    void beginRun(double timelineBeat, double monotonicBeat, double elapsed = 0.0);
    void endRun();

    /// Carry the current run through @p piece without changing state.
    void extendRun(const SyncRange& piece);

    /// Apply whatever the block ran into, at the instant it ran into it.
    void applyEvent(bool fromPending, double timelineBeat, double monotonicBeat);

    PlayState playState_ = PlayState::stopped;
    std::optional<Pending> pending_;

    std::optional<BeatRange> played_;
    std::optional<BeatRange> playedMonotonic_;
    std::optional<BeatRange> lastPlayed_;

    std::optional<double> loopBeats_;

    int loopRetriggerOverflows_ = 0;
};

}  // namespace magda::engine
