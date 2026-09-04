#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <optional>

#include "launch/LaunchHandle.hpp"

/**
 * @file LaunchRequests.hpp
 * @brief How a launch reaches the audio thread from off it (#2305).
 *
 * A queue rather than one of the engine's published values, because a launch is
 * an event: two of them are two retriggers, and a loop length before a play is
 * an order. A value would be re-applied every block until something overwrote
 * it.
 *
 * A request names a slot and never a handle, so nothing here can outlive
 * anything. Handles are made and retired by
 * RuntimeStateStore::publishHandles() on the plan-swap epoch.
 *
 * Written by the publishing thread, read by the audio thread in
 * `advanceLaunchHandles` (SessionLauncher.hpp).
 */

namespace magda::engine {

/// One thing asked of one slot. A play names the slot whose run to join rather
/// than a handle, so the run it joins is the one that handle holds when the
/// request is applied.
struct LaunchRequest {
    enum class Kind : std::uint8_t {
        /// LaunchHandle::play, or playSynced when @ref syncTo is set.
        play,
        stop,

        /// LaunchHandle::releaseSection: stop, and give the track back to its
        /// arrangement.
        backToArrangement,

        /// LaunchHandle::setLooping. On this lane rather than beside it so it
        /// keeps its order: a length set for a clip about to start has to
        /// arrive first.
        loop,
    };

    SlotKey key;
    Kind kind = Kind::play;

    /// The monotonic beat to do it on, or nothing for the next sample. For
    /// Kind::loop this is the re-trigger interval instead, absent to stop
    /// looping.
    std::optional<double> position;

    /// The slot whose run a play joins, for a scene launch. Absent for a plain
    /// launch.
    std::optional<SlotKey> syncTo;

    /// Which handle of @ref key this was asked of, checked against the live
    /// table when it is applied: a slot emptied and refilled in between must
    /// not launch the clip that replaced the one the user clicked.
    std::uint64_t incarnation = 0;
};

/**
 * @brief The lane requests travel down, and what makes a scene one event.
 *
 * A fixed ring, so nothing allocates on either side. The writer fills slots
 * privately and publishes its cursor once per gesture, so the audio thread sees
 * every slot of a scene or none of it.
 */
class LaunchRequestQueue {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "the launch lane's cursors are read on the audio thread");

  public:
    /// Outstanding requests, not lifetime ones: a bound on a burst between two
    /// blocks.
    static constexpr int kCapacity = 1024;

    /**
     * @brief One gesture: what is asked through it arrives together or not at
     *        all. On the publishing thread, committed when it goes out of scope.
     *
     * A gesture too large for the ring is dropped whole and counted (@ref
     * overflows), because half a scene launching leaves those tracks a bar
     * ahead of the rest for as long as they play.
     */
    class Gesture {
      public:
        explicit Gesture(LaunchRequestQueue& queue) : queue_(queue) {
            queue_.beginGesture();
        }

        ~Gesture() {
            queue_.commitGesture();
        }

        Gesture(const Gesture&) = delete;
        Gesture& operator=(const Gesture&) = delete;
        Gesture(Gesture&&) = delete;
        Gesture& operator=(Gesture&&) = delete;

        /**
         * @brief Start @p key playing.
         * @param monotonicBeat When to start, or nothing for as soon as
         *        possible.
         */
        void play(const SlotKey& key, std::optional<double> monotonicBeat = {}) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::play, monotonicBeat, {}});
        }

        /**
         * @brief Start @p key in phase with @p with, which is what a scene
         *        launch is made of.
         * @param monotonicBeat When to start, or nothing for as soon as
         *        possible.
         */
        void playSynced(const SlotKey& key, const SlotKey& with,
                        std::optional<double> monotonicBeat = {}) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::play, monotonicBeat, with});
        }

        /**
         * @brief Stop @p key, leaving its track held by the session (#2302).
         * @param monotonicBeat When to stop, or nothing for as soon as
         *        possible.
         */
        void stop(const SlotKey& key, std::optional<double> monotonicBeat = {}) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::stop, monotonicBeat, {}});
        }

        /// @brief Stop @p key and give its track back to the arrangement (#2302).
        void backToArrangement(const SlotKey& key) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::backToArrangement, {}, {}});
        }

        /**
         * @brief Re-trigger @p key every @p beats.
         * @param beats The interval, or nothing to stop looping.
         */
        void setLooping(const SlotKey& key, std::optional<double> beats) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::loop, beats, {}});
        }

        /**
         * @brief A scene: every slot of @p followers in phase with @p leader.
         *
         * Here rather than left to the caller because the leader's own launch
         * has to be asked for first, or the followers join the run it is about
         * to leave.
         *
         * @param monotonicBeat When to start, or nothing for as soon as
         *        possible.
         */
        template <typename Keys>
        void playScene(const SlotKey& leader, const Keys& followers,
                       std::optional<double> monotonicBeat = {}) {
            play(leader, monotonicBeat);

            for (const auto& follower : followers)
                if (!(follower == leader))
                    playSynced(follower, leader, monotonicBeat);
        }

      private:
        LaunchRequestQueue& queue_;
    };

    /**
     * @brief Read everything committed since the last call, in order.
     *
     * On the audio thread.
     *
     * @param fn Called once per request. Must not allocate or block.
     */
    template <typename Fn> void drain(const Fn& fn) {
        const auto end = written_.load(std::memory_order_acquire);

        for (auto at = read_; at != end; ++at)
            fn(ring_[static_cast<std::size_t>(at % kCapacity)]);

        read_ = end;
        consumed_.store(read_, std::memory_order_release);
    }

    /**
     * @brief Gestures dropped because the ring was full.
     *
     * Counted rather than left silent: a launch that quietly did not happen is
     * otherwise blamed on the audio device. On the publishing thread.
     */
    int overflows() const {
        return overflows_;
    }

    /**
     * @brief Say which handle each slot is on.
     *
     * Called by RuntimeStateStore::publishHandles with the table it is about to
     * publish, so a request made after it carries the new incarnation and one
     * made before carries the old. On the publishing thread.
     */
    void setIncarnations(std::map<SlotKey, std::uint64_t> incarnations) {
        incarnations_ = std::move(incarnations);
    }

    /// @brief What @ref setIncarnations last said about @p key, or zero for a
    ///        slot it has never named.
    std::uint64_t incarnationOf(const SlotKey& key) const {
        const auto it = incarnations_.find(key);
        return it == incarnations_.end() ? 0 : it->second;
    }

  private:
    /// @brief Open a gesture, taking the writer's cursor from the last commit.
    void beginGesture() {
        // Not nestable: the inner commit would publish the outer's half-written
        // gesture.
        assert(!inGesture_);
        inGesture_ = true;

        writing_ = written_.load(std::memory_order_relaxed);
        overflowed_ = false;
    }

    /// @brief Add @p request to the open gesture, or mark it too large to fit.
    void push(LaunchRequest request) {
        if (overflowed_)
            return;

        // Stamped here rather than by the caller: the writer is the thread that
        // publishes handles, so its map is what the live table says.
        request.incarnation = incarnationOf(request.key);

        // Before the write, not at the commit: a full ring would be writing over
        // slots the reader still owns, and rolling the cursor back afterwards
        // does not put their contents back.
        if (writing_ - consumed_.load(std::memory_order_acquire) >=
            static_cast<std::uint64_t>(kCapacity)) {
            overflowed_ = true;
            return;
        }

        ring_[static_cast<std::size_t>(writing_ % kCapacity)] = request;
        ++writing_;
    }

    /// @brief Publish the open gesture, or count it if it overflowed.
    void commitGesture() {
        inGesture_ = false;

        if (overflowed_) {
            ++overflows_;
            return;
        }

        written_.store(writing_, std::memory_order_release);
    }

    std::array<LaunchRequest, kCapacity> ring_{};

    /// Committed by the writer, read by the audio thread.
    std::atomic<std::uint64_t> written_{0};

    /// Committed by the audio thread, and what stops a gesture writing over
    /// requests that have not been read.
    std::atomic<std::uint64_t> consumed_{0};

    /// The writer's own cursor, ahead of @ref written_ while a gesture is open.
    std::uint64_t writing_ = 0;

    /// The reader's own.
    std::uint64_t read_ = 0;

    bool overflowed_ = false;
    int overflows_ = 0;

    bool inGesture_ = false;

    /// The writer's own view. The audio thread compares against the published
    /// table instead.
    std::map<SlotKey, std::uint64_t> incarnations_;
};

}  // namespace magda::engine
