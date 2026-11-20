// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProcessCpuMeter.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>

#if JUCE_WINDOWS
    #include <windows.h>
#else
    #include <sys/resource.h>
#endif

namespace magda {

namespace {

/// CPU time every thread of this process has used, user and system together.
double processCpuSeconds() {
#if JUCE_WINDOWS
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return 0.0;
    const auto seconds = [](const FILETIME& time) {
        const auto ticks =
            (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        return static_cast<double>(ticks) * 1.0e-7;
    };
    return seconds(kernel) + seconds(user);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
    const auto seconds = [](const timeval& time) {
        return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) * 1.0e-6;
    };
    return seconds(usage.ru_utime) + seconds(usage.ru_stime);
#endif
}

constexpr double kShortestSpanSeconds = 0.1;

}  // namespace

double ProcessCpuMeter::sample() {
    const auto cpu = processCpuSeconds();
    const auto wall = juce::Time::getMillisecondCounterHiRes() * 1.0e-3;
    if (lastCpuSeconds_ >= 0.0) {
        const auto span = wall - lastWallSeconds_;
        if (span < kShortestSpanSeconds)
            return share_;
        const auto cores = std::max(juce::SystemStats::getNumCpus(), 1);
        share_ = std::clamp((cpu - lastCpuSeconds_) / (span * cores), 0.0, 1.0);
    }
    lastCpuSeconds_ = cpu;
    lastWallSeconds_ = wall;
    return share_;
}

}  // namespace magda
