#pragma once

#include <chrono>
#include <cstddef>

#include "plan/RenderPlan.hpp"

/**
 * @file BlockProfile.hpp
 * @brief Where a block's time goes, by op kind and phase, when MAGDA_ENGINE_PROFILE is set.
 *
 * Off, it costs one predictable branch per op. On, each thread tallies into its own table, so
 * the profile adds no lock and no shared cache line to the drain it measures, and the tables
 * are summed and printed to stderr at exit. A measuring aid, never a feature.
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

    static void addOp(OpKind kind, std::chrono::steady_clock::duration elapsed);
    static void addPhase(Phase phase, std::chrono::steady_clock::duration elapsed);

    /// A device's own share, by the name it gives.
    static void addDevice(const char* name, std::chrono::steady_clock::duration elapsed);

    /// Print the tallies to stderr. Registered with atexit when the profile is on.
    static void report();

    static constexpr std::size_t kKinds = 64;

  private:
    static const bool enabled_;
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
