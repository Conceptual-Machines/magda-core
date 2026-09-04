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
 * A launch is an event, not a state. Two launches of one slot are two
 * retriggers and a launch followed by a loop length is an order, so what
 * carries them is a queue: written once, read once, never re-read. The engine's
 * other lanes publish a value whole because what they carry is a value, and a
 * request put through one would be applied again every block until something
 * overwrote it.
 *
 * A request names a slot and never a handle. That is what keeps it out of the
 * lifetime question entirely: handles are made and retired by
 * RuntimeStateStore::publishHandles() on the plan-swap epoch, and a request for
 * a slot whose handle has gone finds nothing and does nothing. Nothing here can
 * outlive anything, because nothing here points at anything.
 *
 * One writer and one reader. The writer is whichever thread publishes, which is
 * the single non-realtime thread EngineSession.hpp already names; the reader is
 * the audio thread, in `advanceLaunchHandles`, ahead of every advance in the
 * same pass (SessionLauncher.hpp).
 */

namespace magda::engine {

/**
 * @brief One thing asked of one slot.
 *
 * The four the handle takes, in the shape it takes them. A play carries the
 * slot whose run to join rather than a handle, because the run to join is the
 * one that handle holds when the request is applied, not when it was made.
 */
struct LaunchRequest {
    enum class Kind : std::uint8_t {
        /// LaunchHandle::play, or playSynced when @ref syncTo is set.
        play,
        stop,

        /// LaunchHandle::releaseSection: stop, and give the track back to its
        /// arrangement.
        backToArrangement,

        /// LaunchHandle::setLooping. Here rather than on a channel of its own
        /// so that it keeps its order against the launches around it: a loop
        /// length set for a clip about to start has to arrive first.
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

    /// Which handle of @ref key this was asked of. Stamped when the request is
    /// made and checked against the live table when it is applied, so a slot
    /// emptied and refilled between the two does not launch the clip that
    /// replaced the one the user clicked (#2305 review).
    std::uint64_t incarnation = 0;
};

/**
 * @brief The lane requests travel down, and what makes a scene one event.
 *
 * A fixed ring, so nothing allocates on either side. The writer fills slots
 * privately and publishes its cursor once per gesture, which is what makes a
 * scene atomic: the audio thread either sees every slot of it or none, so eight
 * clips launched together start on one sample rather than over eight
 * consecutive blocks.
 */
class LaunchRequestQueue {
    // Both cursors are read by the thread that does not write them. A lock
    // taken here would be taken on the audio thread, which is the one thing
    // this side of the engine does not do.
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "the launch lane's cursors are read on the audio thread");

  public:
    /// Requests the ring holds between a commit and the audio thread reading
    /// them. A gesture is at most one request per slot in the project, and a
    /// block is milliseconds, so this is a bound on a burst rather than on a
    /// session.
    static constexpr int kCapacity = 1024;

    /**
     * @brief One gesture: what is asked through it arrives together or not at
     *        all.
     *
     * On the publishing thread. Committed when it goes out of scope, which is
     * where the boundary of a gesture is stated rather than left to whoever
     * remembers to call something.
     *
     * A gesture too large for the ring is dropped whole and counted (@ref
     * overflows). Half a scene launching is worse than none: the tracks that
     * made it would be a bar ahead of the ones that did not for as long as they
     * played.
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

        /// Start @p key playing, at @p monotonicBeat or as soon as possible.
        void play(const SlotKey& key, std::optional<double> monotonicBeat = {}) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::play, monotonicBeat, {}});
        }

        /// Start @p key in phase with @p with, which is what a scene launch is.
        void playSynced(const SlotKey& key, const SlotKey& with,
                        std::optional<double> monotonicBeat = {}) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::play, monotonicBeat, with});
        }

        void stop(const SlotKey& key, std::optional<double> monotonicBeat = {}) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::stop, monotonicBeat, {}});
        }

        /// Stop @p key and give its track back to the arrangement (#2302).
        void backToArrangement(const SlotKey& key) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::backToArrangement, {}, {}});
        }

        /// Re-trigger @p key every @p beats, or stop it looping.
        void setLooping(const SlotKey& key, std::optional<double> beats) {
            queue_.push(LaunchRequest{key, LaunchRequest::Kind::loop, beats, {}});
        }

        /// Every slot named in one call, in phase with @p leader, which is what
        /// a scene launch is. Here rather than left to the caller because the
        /// order matters: the leader's own launch has to be asked for first, or
        /// the followers join the run it is about to leave.
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
     * On the audio thread. @p fn is called once per request; it must not
     * allocate or block, which is the same rule everything else on this thread
     * follows.
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
     * Counted rather than left silent, for the reason the clock counts its loop
     * wraps: a launch that quietly did not happen is otherwise blamed on the
     * audio device. On the publishing thread.
     */
    int overflows() const {
        return overflows_;
    }

    /**
     * @brief Which handle each slot is on, as the publishing thread last saw it.
     *
     * Called by RuntimeStateStore::publishHandles with the table it is about to
     * publish, so a request made after it carries the new incarnation and one
     * made before it carries the old. On the publishing thread.
     */
    void setIncarnations(std::map<SlotKey, std::uint64_t> incarnations) {
        incarnations_ = std::move(incarnations);
    }

    /// What @ref setIncarnations last said about @p key, or zero for a slot it
    /// has never named. On the publishing thread.
    std::uint64_t incarnationOf(const SlotKey& key) const {
        const auto it = incarnations_.find(key);
        return it == incarnations_.end() ? 0 : it->second;
    }

  private:
    void beginGesture() {
        // Not nestable, and an assertion rather than a counter: the inner one's
        // commit would publish the outer's half-written gesture, which is the
        // one thing the type exists to prevent.
        assert(!inGesture_);
        inGesture_ = true;

        writing_ = written_.load(std::memory_order_relaxed);
        overflowed_ = false;
    }

    void push(LaunchRequest request) {
        if (overflowed_)
            return;

        // Stamped here rather than by the caller: the map is the writer's own,
        // and the writer is the thread that publishes handles, so what it knows
        // about a slot is what the table it published says.
        request.incarnation = incarnationOf(request.key);

        // Checked before the write and not at the commit: the slots the reader
        // still owns are the ones a full ring would be writing over, and rolling
        // a cursor back afterwards does not put their contents back.
        if (writing_ - consumed_.load(std::memory_order_acquire) >=
            static_cast<std::uint64_t>(kCapacity)) {
            overflowed_ = true;
            return;
        }

        ring_[static_cast<std::size_t>(writing_ % kCapacity)] = request;
        ++writing_;
    }

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

    /// Committed by the audio thread, read by the writer, and the whole of what
    /// stops a gesture writing over requests that have not been read.
    std::atomic<std::uint64_t> consumed_{0};

    /// The writer's own cursor, ahead of @ref written_ for as long as a gesture
    /// is open.
    std::uint64_t writing_ = 0;

    /// The reader's own, so draining does not have to read back what it wrote.
    std::uint64_t read_ = 0;

    bool overflowed_ = false;
    int overflows_ = 0;

    /// Whether a gesture is open, so a nested one is caught rather than
    /// silently publishing half of the one it is inside.
    bool inGesture_ = false;

    /// The writer's own view of which handle each slot is on. Never read on the
    /// audio thread, which compares against the published table instead.
    std::map<SlotKey, std::uint64_t> incarnations_;
};

}  // namespace magda::engine
