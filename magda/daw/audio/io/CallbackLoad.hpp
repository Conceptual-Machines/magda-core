// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>

namespace magda {

/**
 * @brief The audio callback's share of its time budget, averaged over whole windows.
 *
 * JUCE's figure smooths only the last few blocks, so a twice-a-second read of it samples
 * wherever a drum pattern happens to be and swings with the beat.
 */
class CallbackLoad {
  public:
    static constexpr double kWindowSeconds = 0.5;

    /** @brief One callback that took @p busyMs to render @p numSamples. Audio thread. */
    void record(double busyMs, int numSamples, double sampleRate) {
        if (numSamples <= 0 || sampleRate <= 0.0)
            return;
        busyMs_ += busyMs;
        budgetMs_ += numSamples * 1000.0 / sampleRate;
        samples_ += numSamples;
        if (samples_ < sampleRate * kWindowSeconds)
            return;
        lastWindow_.store(busyMs_ / budgetMs_, std::memory_order_relaxed);
        busyMs_ = budgetMs_ = 0.0;
        samples_ = 0;
    }

    /** @brief The most recent complete window, as a proportion of real time. */
    double lastWindow() const {
        return lastWindow_.load(std::memory_order_relaxed);
    }

  private:
    double busyMs_ = 0.0;
    double budgetMs_ = 0.0;
    long long samples_ = 0;
    std::atomic<double> lastWindow_{0.0};
};

}  // namespace magda
