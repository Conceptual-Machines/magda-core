// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/**
 * @file RenderProbe.hpp
 * @brief Counts render ops the OS took off the CPU partway through (#3368).
 *
 * Built only with -DMAGDA_RENDER_PROBE=ON (`make probe-on`); otherwise every call is empty.
 * POSIX only: Windows thread CPU time is too coarse to tell a pause from work.
 */

#include <juce_core/juce_core.h>

#include <cstdint>

#if MAGDA_RENDER_PROBE && !JUCE_WINDOWS
    #include <ctime>
#endif

namespace magda::engine::render_probe {

/** @brief Which render thread ran an op. */
enum class Role : std::uint8_t { other, callback, ahead, worker };

#if MAGDA_RENDER_PROBE && !JUCE_WINDOWS

void setRole(Role role);
void record(int track, bool aheadSide, std::int64_t wallNs, std::int64_t cpuNs);

/** @brief Times one op on the calling thread: wall clock against the thread's CPU time. */
class OpTimer {
  public:
    OpTimer(int track, bool aheadSide) : track_(track), aheadSide_(aheadSide) {}

    ~OpTimer() {
        record(track_, aheadSide_, wallNs() - wall0_, cpuNs() - cpu0_);
    }

    OpTimer(const OpTimer&) = delete;
    OpTimer& operator=(const OpTimer&) = delete;

  private:
    static std::int64_t wallNs() {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return std::int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
    }

    static std::int64_t cpuNs() {
        timespec ts{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return std::int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
    }

    int track_;
    bool aheadSide_;
    std::int64_t wall0_ = wallNs();
    std::int64_t cpu0_ = cpuNs();
};

#else

inline void setRole(Role) {}

/** @brief The probe compiled out. */
struct OpTimer {
    OpTimer(int, bool) {}
};

#endif

/// Message thread: logs the counts and pauses since the last call. Always declared, since the
/// option is the engine's alone and callers outside it cannot see it.
void report();

}  // namespace magda::engine::render_probe
