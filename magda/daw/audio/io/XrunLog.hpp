// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

// After the headers it uses: farbot's fifo includes none of its own.
#include <farbot/fifo.hpp>

namespace magda {

/**
 * @brief When each xrun happened, and what the callbacks before it were doing (#3386, #3401).
 *
 * The device's count rises after the callback it blames, often from another thread, so the
 * audio thread reads it at the top of each callback and sends the last few callbacks with it.
 */
class XrunLog {
  public:
    static constexpr int kHistory = 8;
    static constexpr int kCapacity = 16;

    /** @brief One callback, as the audio thread saw it. */
    struct Callback {
        /// Epoch ms at entry, the unit the render probe's pause lines use.
        std::int64_t atMs = 0;
        float renderMs = 0.0f;
        /// The audio the callback was asked for, in ms: its deadline.
        float lengthMs = 0.0f;
        /// Entry to entry; 0 for the first callback after a device start.
        float sincePreviousMs = 0.0f;
        /// Waiting for the render-ahead thread to hand over or let go of a block.
        float writerWaitMs = 0.0f;
        std::uint8_t blocks = 0;
        /// Rendered whole at the callback, ahead tracks included.
        std::uint8_t wholeBlocks = 0;
        /// The render-ahead thread held the block past the callback's deadline for it.
        std::uint8_t missedBlocks = 0;
        bool started = false;
        bool jumped = false;
        bool published = false;
    };

    /** @brief One rise of the device's count, with the callbacks before it, oldest first. */
    struct Event {
        /// When the audio thread saw the count rise: the callback after the one it blames.
        std::int64_t atMs = 0;
        int count = 0;
        std::array<Callback, kHistory> callbacks{};
        int numCallbacks = 0;
        /// Callbacks since the transport started, jumped, and a plan went live, counted from
        /// the newest in @ref callbacks; -1 when none was seen since the device started.
        int sinceStart = -1;
        int sinceJump = -1;
        int sincePublish = -1;
    };

    /// Any thread, while no callback runs: the next count is a new device's, not a rise.
    void restart() {
        restartPending_.store(true, std::memory_order_release);
    }

    /// Audio thread, at the top of a callback, with the device's running xrun count.
    void check(int deviceCount, std::int64_t nowMs) {
        if (restartPending_.exchange(false, std::memory_order_acq_rel)) {
            lastCount_ = -1;
            numHistory_ = 0;
            sinceStart_ = sinceJump_ = sincePublish_ = -1;
            previousClockMs_ = 0.0;
        }

        const auto last = std::exchange(lastCount_, deviceCount);
        if (last < 0 || deviceCount <= last)
            return;

        Event event;
        event.atMs = nowMs;
        event.count = deviceCount - last;
        event.numCallbacks = numHistory_;
        for (int i = 0; i < numHistory_; ++i)
            event.callbacks[static_cast<std::size_t>(i)] =
                history_[static_cast<std::size_t>((next_ - numHistory_ + i + kHistory) % kHistory)];
        event.sinceStart = sinceStart_;
        event.sinceJump = sinceJump_;
        event.sincePublish = sincePublish_;
        if (!events_.push(std::move(event)))
            dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Audio thread, once a callback has rendered. @p clockMs, a monotonic clock read at its
    /// entry, fills in @ref Callback::sincePreviousMs: epoch ms is too coarse for it.
    void record(Callback callback, double clockMs) {
        callback.sincePreviousMs =
            previousClockMs_ > 0.0 ? static_cast<float>(clockMs - previousClockMs_) : 0.0f;
        previousClockMs_ = clockMs;

        history_[static_cast<std::size_t>(next_)] = callback;
        next_ = (next_ + 1) % kHistory;
        numHistory_ = std::min(numHistory_ + 1, kHistory);

        advance(sinceStart_, callback.started);
        advance(sinceJump_, callback.jumped);
        advance(sincePublish_, callback.published);
    }

    /// One consumer thread: the oldest event not yet taken.
    bool pop(Event& event) {
        return events_.pop(event);
    }

    /// Events lost because nobody took them before the queue filled.
    int dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    static void advance(int& since, bool now) {
        if (now)
            since = 0;
        else if (since >= 0)
            ++since;
    }

    farbot::fifo<Event, farbot::fifo_options::concurrency::single,
                 farbot::fifo_options::concurrency::single>
        events_{kCapacity};
    std::atomic<int> dropped_{0};
    std::atomic<bool> restartPending_{false};

    // Audio thread only.
    int lastCount_ = -1;
    std::array<Callback, kHistory> history_{};
    int next_ = 0;
    int numHistory_ = 0;
    int sinceStart_ = -1;
    int sinceJump_ = -1;
    int sincePublish_ = -1;
    double previousClockMs_ = 0.0;
};

}  // namespace magda
