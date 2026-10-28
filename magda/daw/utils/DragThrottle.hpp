// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * @brief Throttles callback execution during drag operations.
 *
 * Tracks elapsed time since the last fired update and only allows
 * execution when the configured interval has passed. Call check()
 * on every drag event; it returns true at most once per interval.
 *
 * Usage:
 * @code
 * DragThrottle throttle{50};  // 50ms interval
 *
 * void mouseDrag(...) {
 *     // ... compute preview values ...
 *     if (throttle.check()) {
 *         commitToModel();
 *     }
 * }
 *
 * void mouseUp(...) {
 *     throttle.reset();
 *     commitFinal();
 * }
 * @endcode
 */
class DragThrottle {
  public:
    explicit DragThrottle(int intervalMs = 50) : intervalMs_(intervalMs) {}

    /** Returns true if enough time has elapsed since the last successful check. */
    bool check() {
        auto now = juce::Time::currentTimeMillis();
        if (now - lastUpdateTime_ >= intervalMs_) {
            lastUpdateTime_ = now;
            return true;
        }
        return false;
    }

    /** Reset so the next check() will succeed immediately. */
    void reset() {
        lastUpdateTime_ = 0;
    }

  private:
    int intervalMs_;
    juce::int64 lastUpdateTime_ = 0;
};

}  // namespace magda
