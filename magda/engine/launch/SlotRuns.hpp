#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/ClipTypes.hpp"
#include "launch/LaunchHandle.hpp"

/**
 * @file SlotRuns.hpp
 * @brief A slot's runs as edges, stamped by the block that decided them (#2464).
 *
 * The launcher already cuts a block at the sample a launch fires on
 * (LaunchHandle::advance). This is that edge published off the audio thread, so
 * a capture places a run where it actually began rather than where a poll of
 * play state happened to notice it: what was heard and what was captured come
 * from one clock, which is what removes the drift between them as a class.
 *
 * The audio thread's own reader is `slotRun` in SessionLauncher.hpp, which
 * needs no queue: a take reads the block status of the handle it follows.
 */

namespace magda::engine {

/// The model material a run rendered. Revision distinguishes edits which keep
/// the same clip ID; the host owns the revision and its immutable model copy.
struct CaptureSource {
    ClipId clipId = INVALID_CLIP_ID;
    std::uint64_t revision = 0;

    bool operator==(const CaptureSource&) const = default;
};

/// The last complete block boundary the audio thread published with its edges.
struct SlotRunBoundary {
    SamplePosition at;
    double timelineBeat = 0.0;
    double monotonicBeat = 0.0;
};

/** @brief One edge of one slot's run. */
struct SlotRunEvent {
    enum class Kind : std::uint8_t { began, ended, captureBegan, captureEnded };

    SlotKey key;
    Kind kind = Kind::began;

    /// Which handle of @ref key it happened on, so a capture cannot join a run
    /// of the clip that replaced the one it was following.
    std::uint64_t incarnation = 0;

    CaptureSource source;

    /// Phase into @ref source at this run edge. Non-zero when a material edit
    /// splits a handle without restarting its playback position.
    double offsetBeats = 0.0;

    /// The transport sample it happened on. Two runs that began on this sample
    /// began together, whatever the timeline did in between.
    SamplePosition at;

    /// Where that sample sits on the timeline, and on the count no wrap takes
    /// back. A run is placed by the first and measured by the second.
    double timelineBeat = 0.0;
    double monotonicBeat = 0.0;

    std::uint64_t recordingGeneration = 0;
};

/**
 * @brief The lane those edges travel down, audio thread to publishing thread.
 *
 * A fixed ring, so neither side allocates. The reverse direction of
 * LaunchRequests.hpp and the same shape: a full ring drops the edge and counts
 * it rather than writing over one nobody has read.
 */
class SlotRunQueue {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "a run lane's cursors are written on the audio thread and must not take a lock");
    static_assert(std::atomic<std::int64_t>::is_always_lock_free,
                  "the lane's sample boundary is written on the audio thread as well");
    static_assert(std::atomic<double>::is_always_lock_free,
                  "the lane's watermark is written on the audio thread as well");

  public:
    /// Outstanding edges, not lifetime ones: a bound on what a scene launch and
    /// the frame it lands in can put between two drains.
    static constexpr int kCapacity = 512;

    /// @brief Publish @p event. Audio thread.
    void push(const SlotRunEvent& event) {
        const auto at = written_.load(std::memory_order_relaxed);

        if (at - consumed_.load(std::memory_order_acquire) >=
            static_cast<std::uint64_t>(kCapacity)) {
            overflows_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        ring_[static_cast<std::size_t>(at % kCapacity)] = event;
        written_.store(at + 1, std::memory_order_release);
    }

    /**
     * @brief Read everything published since the last call, and say how far the
     *        lane has been reported.
     *
     * On the publishing thread. @p fn is called once per edge, in order.
     *
     * The boundary carries the edge cursor published with it. An edge from the
     * next block can therefore never be consumed against the previous block's
     * position, even when the audio thread advances during this drain.
     */
    template <typename Fn> SlotRunBoundary drain(const Fn& fn) {
        const auto until = boundary();
        const auto end = until.written;

        for (auto at = read_; at != end; ++at)
            fn(ring_[static_cast<std::size_t>(at % kCapacity)]);

        read_ = end;
        consumed_.store(read_, std::memory_order_release);
        return until.position;
    }

    /**
     * @brief Say every edge up to @p monotonicBeat has been published.
     *
     * Audio thread, once a block, after that block's edges. What a capture ends
     * a run at when it is disarmed, and it is handed out by @ref drain alone:
     * pairing it with a drain is the whole of the contract, so there is no
     * accessor for it to be paired with anything else.
     */
    void reachedBeat(double monotonicBeat) {
        reached({.timelineBeat = monotonicBeat, .monotonicBeat = monotonicBeat});
    }

    /// Publish all faces of one completed block boundary. Audio thread.
    void reached(const SlotRunBoundary& boundary) {
        boundarySequence_.fetch_add(1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        boundarySample_.store(boundary.at.sample, std::memory_order_relaxed);
        boundaryTimeline_.store(boundary.timelineBeat, std::memory_order_relaxed);
        boundaryMonotonic_.store(boundary.monotonicBeat, std::memory_order_relaxed);
        boundaryWritten_.store(written_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        boundarySequence_.fetch_add(1, std::memory_order_release);
    }

    /// Edges the ring had no room for. Above zero and a capture is missing a
    /// run; counted rather than left silent, like every other overflow here.
    int overflows() const {
        return overflows_.load(std::memory_order_relaxed);
    }

  private:
    std::array<SlotRunEvent, kCapacity> ring_{};

    /// Written by the audio thread, read by the publishing thread.
    std::atomic<std::uint64_t> written_{0};

    /// Published by the reader, and what stops the writer overwriting an edge
    /// that has not been read.
    std::atomic<std::uint64_t> consumed_{0};

    /// The reader's own cursor.
    std::uint64_t read_ = 0;

    struct PublishedBoundary {
        SlotRunBoundary position;
        std::uint64_t written = 0;
    };

    /// A publishing-thread seqlock read. The audio writer never waits.
    PublishedBoundary boundary() const {
        for (;;) {
            const auto before = boundarySequence_.load(std::memory_order_acquire);
            if ((before & 1U) != 0U)
                continue;

            const PublishedBoundary result{
                .position = {.at = SamplePosition{boundarySample_.load(std::memory_order_relaxed)},
                             .timelineBeat = boundaryTimeline_.load(std::memory_order_relaxed),
                             .monotonicBeat = boundaryMonotonic_.load(std::memory_order_relaxed)},
                .written = boundaryWritten_.load(std::memory_order_relaxed)};
            std::atomic_thread_fence(std::memory_order_acquire);
            const auto after = boundarySequence_.load(std::memory_order_relaxed);
            if (before == after)
                return result;
        }
    }

    std::atomic<std::uint64_t> boundarySequence_{0};
    std::atomic<std::int64_t> boundarySample_{0};
    std::atomic<double> boundaryTimeline_{0.0};
    std::atomic<double> boundaryMonotonic_{0.0};
    std::atomic<std::uint64_t> boundaryWritten_{0};

    std::atomic<int> overflows_{0};
};

}  // namespace magda::engine
