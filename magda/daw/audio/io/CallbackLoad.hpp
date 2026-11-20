// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace magda {

/**
 * @brief The share of each audio period spent rendering, averaged over whole windows.
 *
 * JUCE's figure smooths only the last few blocks, so a twice-a-second read of it samples
 * wherever a drum pattern happens to be and swings with the beat. The render-ahead thread's
 * time counts too (#3357): with nothing played the whole mix renders there.
 */
class CallbackLoad {
  public:
    static constexpr double kWindowSeconds = 0.5;

    /** @brief One window's shares of real time. */
    struct Window {
        /// Counts windows from 1; 0 is none yet.
        std::uint64_t index = 0;
        double callback = 0.0;
        double ahead = 0.0;
        /// The slowest single block on each thread, over that block's own length.
        double worstCallback = 0.0;
        double worstAhead = 0.0;

        double total() const {
            return callback + ahead;
        }
    };

    /**
     * @brief One callback that took @p busyMs to render @p numSamples, while the ahead thread
     *        spent @p aheadMs with a worst block of @p aheadWorst. Audio thread.
     */
    void record(double busyMs, int numSamples, double sampleRate, double aheadMs = 0.0,
                double aheadWorst = 0.0) {
        if (numSamples <= 0 || sampleRate <= 0.0)
            return;
        const auto blockMs = numSamples * 1000.0 / sampleRate;
        busyMs_ += busyMs;
        aheadMs_ += aheadMs;
        budgetMs_ += blockMs;
        worstCallback_ = std::max(worstCallback_, busyMs / blockMs);
        worstAhead_ = std::max(worstAhead_, aheadWorst);
        samples_ += numSamples;
        if (samples_ < sampleRate * kWindowSeconds)
            return;

        const auto sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        callback_.store(busyMs_ / budgetMs_, std::memory_order_relaxed);
        ahead_.store(aheadMs_ / budgetMs_, std::memory_order_relaxed);
        worstCallbackOut_.store(worstCallback_, std::memory_order_relaxed);
        worstAheadOut_.store(worstAhead_, std::memory_order_relaxed);
        sequence_.store(sequence + 2, std::memory_order_release);

        busyMs_ = aheadMs_ = budgetMs_ = worstCallback_ = worstAhead_ = 0.0;
        samples_ = 0;
    }

    /** @brief The most recent complete window, both threads, as a proportion of real time. */
    double lastWindow() const {
        return window().total();
    }

    /** @brief The most recent complete window. Any thread. */
    Window window() const {
        for (;;) {
            const auto before = sequence_.load(std::memory_order_acquire);
            if ((before & 1U) != 0)
                continue;
            Window read{.index = before / 2,
                        .callback = callback_.load(std::memory_order_relaxed),
                        .ahead = ahead_.load(std::memory_order_relaxed),
                        .worstCallback = worstCallbackOut_.load(std::memory_order_relaxed),
                        .worstAhead = worstAheadOut_.load(std::memory_order_relaxed)};
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) == before)
                return read;
        }
    }

  private:
    double busyMs_ = 0.0;
    double aheadMs_ = 0.0;
    double budgetMs_ = 0.0;
    double worstCallback_ = 0.0;
    double worstAhead_ = 0.0;
    long long samples_ = 0;

    // A sequence lock: odd while the audio thread writes a window.
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<double> callback_{0.0};
    std::atomic<double> ahead_{0.0};
    std::atomic<double> worstCallbackOut_{0.0};
    std::atomic<double> worstAheadOut_{0.0};
};

}  // namespace magda
