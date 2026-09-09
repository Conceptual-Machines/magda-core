#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

/** @brief One edge of one slot's run. */
struct SlotRunEvent {
    enum class Kind : std::uint8_t { began, ended };

    SlotKey key;
    Kind kind = Kind::began;

    /// Which handle of @ref key it happened on, so a capture cannot join a run
    /// of the clip that replaced the one it was following.
    std::uint64_t incarnation = 0;

    /// The transport sample it happened on. Two runs that began on this sample
    /// began together, whatever the timeline did in between.
    SamplePosition at;

    /// Where that sample sits on the timeline, and on the count no wrap takes
    /// back. A run is placed by the first and measured by the second.
    double timelineBeat = 0.0;
    double monotonicBeat = 0.0;
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
     * @brief Read everything published since the last call, in order.
     *
     * On the publishing thread. @p fn is called once per edge.
     */
    template <typename Fn> void drain(const Fn& fn) {
        const auto end = written_.load(std::memory_order_acquire);

        for (auto at = read_; at != end; ++at)
            fn(ring_[static_cast<std::size_t>(at % kCapacity)]);

        read_ = end;
        consumed_.store(read_, std::memory_order_release);
    }

    /**
     * @brief Say every edge up to @p monotonicBeat has been published.
     *
     * Audio thread, once a block, after that block's edges. What a capture ends
     * a run at when it is disarmed: read after a drain, it cannot be a beat
     * whose edges the capture has not seen, which is what makes a boundary
     * ordered against the lane rather than a second clock to disagree with it
     * (#2464 review).
     */
    void reachedBeat(double monotonicBeat) {
        reached_.store(monotonicBeat, std::memory_order_release);
    }

    /// The beat @ref reachedBeat last named. Publishing thread, after a drain.
    double reached() const {
        return reached_.load(std::memory_order_acquire);
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

    /// How far the audio thread has reported, in monotonic beats.
    std::atomic<double> reached_{0.0};

    std::atomic<int> overflows_{0};
};

}  // namespace magda::engine
