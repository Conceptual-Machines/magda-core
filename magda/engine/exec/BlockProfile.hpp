#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "plan/RenderPlan.hpp"

/**
 * @file BlockProfile.hpp
 * @brief Where a block's time goes, by op kind and phase, when MAGDA_ENGINE_PROFILE is set.
 *
 * Off, it costs one predictable branch per op. On, it tallies nanoseconds into atomics and
 * prints the table to stderr at exit. A measuring aid, never a feature.
 */

namespace magda::engine {

class BlockProfile {
  public:
    enum Phase : int {
        BeginBlock = 0,
        MidiPrefix,
        ResolveParameters,
        Drain,
        SerialTail,
        WholeBlock,
        PhaseCount
    };

    static bool enabled() {
        return enabled_;
    }

    static void addOp(OpKind kind, std::chrono::steady_clock::duration elapsed) {
        add(opNanos_[static_cast<std::size_t>(kind)], opCounts_[static_cast<std::size_t>(kind)],
            elapsed);
    }

    static void addPhase(Phase phase, std::chrono::steady_clock::duration elapsed) {
        add(phaseNanos_[static_cast<std::size_t>(phase)],
            phaseCounts_[static_cast<std::size_t>(phase)], elapsed);
    }

    /// A device's own share, by the name it gives. Takes a lock, so only when the profile is on.
    static void addDevice(const char* name, std::chrono::steady_clock::duration elapsed);

    /// Print the tallies to stderr. Registered with atexit when the profile is on.
    static void report();

  private:
    static void add(std::atomic<std::uint64_t>& nanos, std::atomic<std::uint64_t>& count,
                    std::chrono::steady_clock::duration elapsed) {
        nanos.fetch_add(static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
                        std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
    }

    static constexpr std::size_t kKinds = 64;

    static const bool enabled_;
    static std::array<std::atomic<std::uint64_t>, kKinds> opNanos_;
    static std::array<std::atomic<std::uint64_t>, kKinds> opCounts_;
    static std::array<std::atomic<std::uint64_t>, PhaseCount> phaseNanos_;
    static std::array<std::atomic<std::uint64_t>, PhaseCount> phaseCounts_;
};

/// Times a scope into @p sink when the profile is on.
template <typename Sink> class ProfileScope {
  public:
    explicit ProfileScope(Sink sink)
        : sink_(sink),
          started_(BlockProfile::enabled() ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{}) {}
    ~ProfileScope() {
        if (BlockProfile::enabled())
            sink_(std::chrono::steady_clock::now() - started_);
    }

  private:
    Sink sink_;
    std::chrono::steady_clock::time_point started_;
};

}  // namespace magda::engine
