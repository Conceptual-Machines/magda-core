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
 * @brief One block, as the launcher sees it.
 *
 * Four faces of the same stretch, which is what BlockInfo carries and where
 * they were derived together. Beats and seconds each have a timeline form and a
 * monotonic one: the timeline forms are where the material sits and go
 * backwards when the loop wraps, the monotonic forms only ever move forwards.
 *
 * A queued position is named in monotonic beats, because a launch is quantized
 * musically. How far into its material a run has got is measured in monotonic
 * seconds, because that is elapsed time and a file is read at a rate. Neither
 * may be derived from the other: a tempo map answers where a beat is, not how
 * long something has been going, and after a wrap the two ends of a run are not
 * even in the same cycle of the timeline (#2324).
 */
struct SyncRange {
    BeatRange timeline;
    BeatRange monotonic;
    SecondsRange seconds;
    SecondsRange monotonicSeconds;
};

/**
 * @brief One instant, in the faces a run's origin is remembered in.
 *
 * Passed together so a run cannot be started with three faces of one instant
 * and one of another, which is the shape of every bug this domain exists to
 * prevent.
 */
struct SyncPoint {
    double timelineBeat = 0.0;
    double monotonicBeat = 0.0;
    double monotonicSeconds = 0.0;
};

/**
 * @brief Where a run began, in the two domains a source reads it against.
 *
 * Together because they are one instant. A source positions beat-domain
 * material against the first and seconds-domain material against the second,
 * and a run whose two faces came from different instants would put a clip's
 * notes and its samples in different places.
 */
struct RunOrigin {
    double beat = 0.0;
    double seconds = 0.0;

    bool operator==(const RunOrigin&) const = default;
};

/**
 * @brief One piece of a block, and what the slot was doing over it.
 *
 * A piece sounds exactly when there is a run behind it, so where that run began
 * is the whole answer and there is no separate flag to disagree with it.
 */
struct BlockPiece {
    BeatRange range;

    /**
     * @brief Where the run sounding over this piece began, or nothing.
     *
     * On both of the block's own axes, because a source subtracts one from the
     * beat it is handed and the other from the seconds, and the two are one
     * instant (RunOrigin).
     *
     * Virtual rather than historical: once the timeline has wrapped, the
     * instant the run really started on is no longer in the cycle the block
     * belongs to, so each face is projected forward from its own monotonic
     * domain instead. Either can therefore sit before the block, or before
     * zero.
     */
    std::optional<RunOrigin> origin;

    /// Whether the slot sounded over this piece.
    bool playing() const {
        return origin.has_value();
    }
};

/**
 * @brief What one block was, once the handle has been advanced over it.
 *
 * Two pieces rather than one play state, because a launch is quantized to a
 * beat and a beat lands wherever it lands inside a block. A handle that
 * answered per block would start every clip on a block boundary: at 512
 * samples and 120 bpm that is up to 11 ms of slop, it differs per track, and
 * removing exactly that slop is what quantized launch is for.
 *
 * The instant they are cut at is whatever the block ran into: a launch, a stop,
 * or a loop re-trigger. A block that ran into nothing is one piece.
 */
struct SplitStatus {
    /// The block up to the instant it ran into, or the whole of it when it ran
    /// into nothing.
    BlockPiece beforeEvent;

    /// What was left after that instant, or nothing when there was no instant.
    /// Absent rather than empty, so a caller cannot read a second piece that
    /// was never played.
    std::optional<BlockPiece> afterEvent;

    /// Whether the slot is sounding when the block ends, which is what decides
    /// whether a stop owes anything: the second piece when there is one, and
    /// the first when the block was not cut.
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
     * Both faces, given rather than converted. A nudge moves where in the
     * material the run is, and the material has a beat position and a seconds
     * position that are not the same number; working one out from the other
     * here would be the round trip this domain exists to avoid. The caller has
     * the block and therefore has both.
     */
    void nudge(double beats, double seconds);

    /**
     * @brief Advance over one block and say what it was.
     *
     * The audio thread's call, once per block, in order. A block passed out of
     * order or skipped puts the played range somewhere the material never was.
     *
     * Once per block and not once per reader. A slot has two sources rendering
     * it, audio and MIDI, and a handle advanced by each of them would be moved
     * twice through every block. So the launcher advances every handle before
     * anything renders (SessionLauncher.hpp) and both sources read @ref
     * blockStatus.
     */
    SplitStatus advance(const SyncRange& range);

    /**
     * @brief What the last advance said, for whoever renders the slot.
     *
     * The other half of advancing centrally: the answer is worked out once and
     * read by both of a slot's sources. Default-constructed until the first
     * advance, which is a slot that has not been rendered yet and is therefore
     * not playing.
     */
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
    /// a source reading a file at the file's own speed needs and the one
    /// quantity no tempo map can answer.
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
        /// three faces, because a handle that joined in beats alone would
        /// agree about the bar and disagree about the sample.
        std::optional<double> syncedTimelineOrigin;
        std::optional<double> syncedMonotonicOrigin;
        std::optional<double> syncedMonotonicSecondsOrigin;
    };

    /// Begin a run whose origin is @p at, and which is already @p elapsedBeats
    /// and @p elapsedSeconds into itself. Non-zero only for a synced launch,
    /// which joins a run rather than starting one, and both faces are given
    /// because a synced handle has to agree with the one it joins in both.
    void beginRun(const SyncPoint& at, double elapsedBeats = 0.0, double elapsedSeconds = 0.0);
    void endRun();

    /// Carry the current run through @p piece without changing state.
    void extendRun(const SyncRange& piece);

    /// The run's origin expressed in @p piece's own cycle of the timeline,
    /// on each of the two axes a source can be handed.
    double virtualStart(const SyncRange& piece) const;
    double virtualStartSeconds(const SyncRange& piece) const;

    /// Apply whatever the block ran into, at the instant it ran into it.
    void applyEvent(bool fromPending, const SyncPoint& at);

    /// The advance itself. Wrapped so the answer is kept for @ref blockStatus
    /// in one place rather than at each of the several ways out of it.
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
