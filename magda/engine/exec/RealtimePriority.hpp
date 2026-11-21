// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file RealtimePriority.hpp
 * @brief Real-time scheduling for a Linux thread: directly within RLIMIT_RTPRIO, else from rtkit.
 */

#include <juce_core/juce_core.h>

#include <cstdint>

namespace magda::engine::realtime {

/** @brief What raise() was granted. */
struct Grant {
    int priority = 0;  ///< 0 when the thread stays time-shared.
    bool viaRtkit = false;
    int rlimit = 0;  ///< RLIMIT_RTPRIO's soft limit, for the log.
};

/**
 * @brief Makes thread @p threadId (a kernel tid in this process) SCHED_RR at @p priority.
 *
 * Asks directly, capped at RLIMIT_RTPRIO, and when that is 0 or refused asks rtkit, capped at
 * its MaxRealtimePriority. Blocks on D-Bus in the rtkit case, so never call it from the audio
 * thread. Off Linux it does nothing and grants 0.
 */
Grant raise(std::int64_t threadId, int priority);

/** @brief @p grant for the log: "SCHED_RR at priority 80", or why it stays time-shared. */
juce::String describe(const Grant& grant);

/** @brief The calling thread's kernel tid, or 0 off Linux. */
std::int64_t currentThreadId();

/** @brief True when the calling thread is SCHED_FIFO or SCHED_RR, and always off Linux. */
bool currentThreadIsRealtime();

}  // namespace magda::engine::realtime
