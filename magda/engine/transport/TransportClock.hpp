#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

#include "exec/RenderContext.hpp"
#include "transport/TransportState.hpp"

/**
 * @file TransportClock.hpp
 * @brief The sample clock: where the timeline is, and how far it moved.
 *
 * The audio device counts samples, the model counts beats, and this is the one
 * place the two meet. It holds an anchor (a position on the timeline, and the
 * sample count at which the cursor was there) and derives everything else from
 * the samples since: the cursor is a function of the sample count, never a sum
 * of per-block steps, so it cannot drift and a block cut in half lands in
 * exactly the same place as the whole one would have.
 *
 * It also decides where a callback stops being one stretch of timeline. A loop
 * wrap is a discontinuity, so the callback is cut at it and each piece is
 * rendered as its own block; ops therefore never see a block that jumps in the
 * middle, and never have to work out which of their samples came from where.
 *
 * Audio thread, one caller. The position it publishes is readable from
 * anywhere.
 */

namespace magda::engine {

class TransportClock {
  public:
    /// One stretch of a callback that the timeline runs through in one piece.
    struct Segment {
        BlockInfo block;

        /// Where it begins in the callback's buffer.
        int startSample = 0;

        /// The cursor is still short of the play position, rolling in. The
        /// metronome sounds through a count-in whether or not it is switched
        /// on, which is the only thing that reads this.
        bool countingIn = false;
    };

    /**
     * @brief Move the cursor over @p numSamples and say what they cover.
     *
     * On the audio thread. The returned segments cover the callback exactly,
     * in order, with no gaps. There is always at least one for a callback with
     * samples in it: a stopped transport renders a block that stands still
     * rather than no block at all.
     *
     * The sample rate arrives per call rather than through a prepare, because
     * everything else about the clock lives on this thread and a rate settled
     * from another one would be a race for the sake of an argument. A rate that
     * has changed re-anchors: the cursor is a position in seconds, which
     * survives it, plus a count of samples, which does not.
     *
     * The span points at storage owned by the clock and is valid until the next
     * call.
     */
    std::span<const Segment> advance(const TransportSnapshot& snapshot, double sampleRate,
                                     int numSamples);

    /// Where the cursor is, in beats. Written on the audio thread, readable
    /// from anywhere: this is what a playhead is drawn from.
    double positionBeats() const {
        return positionBeats_.load(std::memory_order_relaxed);
    }

    /// Whether the transport is rolling, as the clock currently has it.
    bool isPlaying() const {
        return playingPublic_.load(std::memory_order_relaxed);
    }

    /// Musical time the transport has rolled through since the clock began,
    /// in beats that never go backwards. Audio thread, and the domain a
    /// queued launch names its position in (#2300).
    double monotonicBeat() const {
        return monotonicBeat_;
    }

    /// Wall-clock time the transport has rolled through since the clock began,
    /// in seconds that never go backwards. Audio thread, and the domain a run
    /// measures how far into its material it is in (#2324).
    double monotonicSeconds() const {
        return monotonicSeconds_;
    }

    /**
     * @brief Callbacks in which a loop was too short to be honoured.
     *
     * A loop of a few samples would wrap more times in one callback than there
     * is room to render separately. The rest of the callback plays straight
     * through instead, and this counts it, because a loop that quietly stopped
     * looping is exactly the kind of thing that gets blamed on the audio
     * device.
     */
    int loopWrapOverflows() const {
        return loopWrapOverflows_.load(std::memory_order_relaxed);
    }

  private:
    /// How many pieces one callback may be cut into. A loop shorter than a
    /// block is a stutter effect rather than a mistake, so several wraps in a
    /// callback are rendered rather than refused; past this the loop is shorter
    /// than the render can express and the overflow count says so.
    static constexpr int kMaxSegmentsPerBlock = 32;

    /// The timeline position, in seconds, that @ref samplesSinceAnchor_ counts
    /// from.
    void anchorTo(const TempoMap& tempo, double beat);

    /// Where the cursor would be @p samples after the anchor, in seconds and
    /// in beats. The seconds are the primitive: the anchor is a position in
    /// seconds and the device counts samples, so this is the whole of the
    /// conversion and the beats are that answer read through a tempo map.
    double secondsAfter(std::int64_t samples) const;
    double beatAfter(const TempoMap& tempo, std::int64_t samples) const;

    /// Samples from the cursor until the timeline reaches @p beat, rounded up
    /// so the answer is the first sample at or past it. Negative when it is
    /// already behind.
    std::int64_t samplesUntil(const TempoMap& tempo, double beat) const;

    void applyRequest(const TransportSnapshot& snapshot);
    void followTempo(const TempoMap& tempo);

    double sampleRate_ = 44100.0;

    std::array<Segment, kMaxSegmentsPerBlock> segments_{};
    int segmentCount_ = 0;

    /// The request that put the cursor where it is. A snapshot carrying this
    /// same generation is a republication, not a new instruction.
    std::uint64_t generation_ = 0;

    bool playing_ = false;
    double anchorSeconds_ = 0.0;
    std::int64_t samplesSinceAnchor_ = 0;

    /// The tempo map the anchor was computed against. A different one means
    /// the seconds under the cursor moved while the beat it stands on did not.
    std::uint64_t tempoFingerprint_ = 0;

    /// Where the cursor is, kept here as well as published because re-anchoring
    /// onto an edited tempo map needs the beat and can no longer ask the map
    /// that produced it.
    double positionBeat_ = 0.0;

    bool countingIn_ = false;
    double countInUntilBeat_ = 0.0;

    /// Whether the next block continues the last one.
    bool continuous_ = false;

    /// Musical time rolled through since the clock began, never decreasing.
    /// Accumulated from what each playing segment covered rather than derived
    /// from the cursor, which is the only way it survives the wraps and
    /// locates that move the cursor backwards.
    double monotonicBeat_ = 0.0;

    /// The same for wall-clock time, and accumulated from the samples rather
    /// than from the beats beside it. A block's own two faces agree, so the
    /// difference is not in this line but in what it refuses: a caller with two
    /// monotonic beats and a tempo map cannot get seconds out of them, because
    /// the map answers where a beat is and not how long a run has been going.
    double monotonicSeconds_ = 0.0;

    std::atomic<double> positionBeats_{0.0};
    std::atomic<bool> playingPublic_{false};
    std::atomic<int> loopWrapOverflows_{0};
};

}  // namespace magda::engine
