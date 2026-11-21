// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later
#include "exec/RenderProbe.hpp"

#if !MAGDA_RENDER_PROBE

void magda::engine::render_probe::report() {}

#else

    #include <array>
    #include <atomic>
    #include <chrono>
    #include <thread>

    #if JUCE_WINDOWS
        #include <intrin.h>
        #include <windows.h>
    #endif

namespace magda::engine::render_probe {
namespace {

/// A pause is wall time the thread did not run for; under this it is scheduling noise.
constexpr std::int64_t kPauseNs = 1'000'000;
/// Pauses this long are logged one by one, to line up with engine.health's xrun times.
constexpr std::int64_t kLoggedPauseNs = 2'000'000;
constexpr int kRoles = 4;

struct Counts {
    std::atomic<std::uint64_t> ops{0};
    std::atomic<std::uint64_t> paused{0};
    std::atomic<std::int64_t> worstNs{0};
};

struct Pause {
    std::atomic<std::uint64_t> seq{0};
    std::int64_t atMs = 0;  // epoch ms, the unit engine.health stamps xruns with
    std::int64_t pauseNs = 0;
    std::int64_t wallNs = 0;
    int track = 0;
    Role role = Role::other;
    bool aheadSide = false;
};

std::array<Counts, kRoles> counts;
std::array<Pause, 256> pauses;
std::atomic<std::uint64_t> nextPause{0};
std::uint64_t reportedPause = 0;
thread_local Role threadRole = Role::other;

    #if JUCE_WINDOWS
double qpcNsPerTick() {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    return 1e9 / double(freq.QuadPart);
}

const double nsPerQpcTick = qpcNsPerTick();
/// Zero until the startup thread has measured the TSC rate; ops before that are not counted.
std::atomic<double> nsPerCycle{0.0};

// QueryThreadCycleTime counts TSC cycles: their rate is measured over 100 ms, off the audio thread.
const bool cycleRateStarted = [] {
    std::thread([] {
        LARGE_INTEGER qpc0{}, qpc1{};
        QueryPerformanceCounter(&qpc0);
        const auto tsc0 = __rdtsc();
        Sleep(100);
        QueryPerformanceCounter(&qpc1);
        const auto tsc1 = __rdtsc();
        nsPerCycle.store(double(qpc1.QuadPart - qpc0.QuadPart) * nsPerQpcTick /
                         double(tsc1 - tsc0));
    }).detach();
    return true;
}();
    #endif

const char* nameOf(Role role) {
    switch (role) {
        case Role::callback:
            return "callback";
        case Role::ahead:
            return "render ahead";
        case Role::worker:
            return "worker";
        case Role::other:
            break;
    }
    return "other";
}

}  // namespace

    #if JUCE_WINDOWS
std::int64_t wallTicks() {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    return qpc.QuadPart;
}

std::int64_t cpuTicks() {
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return static_cast<std::int64_t>(cycles);
}
    #endif

void setRole(Role role) {
    threadRole = role;
}

void record(int track, bool aheadSide, std::int64_t wallTicks, std::int64_t cpuTicks) {
    #if JUCE_WINDOWS
    const auto cycleNs = nsPerCycle.load(std::memory_order_relaxed);
    if (cycleNs == 0.0)
        return;
    const auto wallNs = static_cast<std::int64_t>(double(wallTicks) * nsPerQpcTick);
    const auto cpuNs = static_cast<std::int64_t>(double(cpuTicks) * cycleNs);
    #else
    const auto wallNs = wallTicks;
    const auto cpuNs = cpuTicks;
    #endif
    auto& c = counts[static_cast<std::size_t>(threadRole)];
    c.ops.fetch_add(1, std::memory_order_relaxed);
    const auto pauseNs = wallNs - cpuNs;
    if (pauseNs < kPauseNs)
        return;

    c.paused.fetch_add(1, std::memory_order_relaxed);
    auto worst = c.worstNs.load(std::memory_order_relaxed);
    while (pauseNs > worst && !c.worstNs.compare_exchange_weak(worst, pauseNs))
        ;

    // A seqlock slot: the reader skips one that is mid-write or was overwritten.
    if (pauseNs < kLoggedPauseNs)
        return;
    const auto n = nextPause.fetch_add(1, std::memory_order_relaxed);
    auto& slot = pauses[n % pauses.size()];
    slot.seq.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.atMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
    slot.pauseNs = pauseNs;
    slot.wallNs = wallNs;
    slot.track = track;
    slot.role = threadRole;
    slot.aheadSide = aheadSide;
    slot.seq.store(n + 1, std::memory_order_release);
}

void report() {
    juce::String line = "[render probe]";
    bool any = false;
    for (int r = 0; r < kRoles; ++r) {
        auto& c = counts[static_cast<std::size_t>(r)];
        const auto ops = c.ops.exchange(0, std::memory_order_relaxed);
        const auto paused = c.paused.exchange(0, std::memory_order_relaxed);
        const auto worst = c.worstNs.exchange(0, std::memory_order_relaxed);
        if (ops == 0)
            continue;
        any = true;
        line << " " << nameOf(static_cast<Role>(r)) << ": " << juce::String(ops) << " ops, "
             << juce::String(paused) << " paused ("
             << juce::String(100.0 * static_cast<double>(paused) / static_cast<double>(ops), 1)
             << "%), worst " << juce::String(static_cast<double>(worst) / 1e6, 2) << " ms;";
    }
    if (any)
        juce::Logger::writeToLog(line);

    const auto end = nextPause.load(std::memory_order_acquire);
    if (end - reportedPause > pauses.size())
        reportedPause = end - pauses.size();
    for (; reportedPause < end; ++reportedPause) {
        auto& slot = pauses[reportedPause % pauses.size()];
        if (slot.seq.load(std::memory_order_acquire) != reportedPause + 1)
            continue;
        const Pause copy{.atMs = slot.atMs,
                         .pauseNs = slot.pauseNs,
                         .wallNs = slot.wallNs,
                         .track = slot.track,
                         .role = slot.role,
                         .aheadSide = slot.aheadSide};
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.seq.load(std::memory_order_relaxed) != reportedPause + 1)
            continue;
        juce::Logger::writeToLog("[render probe] pause at " + juce::String(copy.atMs) + ": " +
                                 nameOf(copy.role) + ", track " + juce::String(copy.track) +
                                 (copy.aheadSide ? " (ahead side)" : "") + ", paused " +
                                 juce::String(static_cast<double>(copy.pauseNs) / 1e6, 2) + " of " +
                                 juce::String(static_cast<double>(copy.wallNs) / 1e6, 2) + " ms");
    }
}

}  // namespace magda::engine::render_probe

#endif
