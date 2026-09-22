#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include "ParityMeasure.hpp"
#include "PumpDevice.hpp"

/**
 * @file PumpThread.hpp
 * @brief The device's clock: pulls the pump once per block period on a realtime thread (#2082).
 *
 * Runs from the moment it starts, as an interface does, and times callbacks only inside a span
 * the message thread asks for. A callback that overruns its period is not made up for: the next
 * one waits for the next period, as it would on hardware.
 */

namespace magda::parity {

/// What a measured span found.
struct SpanResult {
    BlockTimeSummary blocks;

    /// Callbacks that took longer than their block lasts: a dropout on a real interface.
    int overruns = 0;

    /// CPU every thread of the process used over the span, per block, in microseconds.
    double processCpuUsPerBlock = 0.0;

    /// The loudest sample the span produced, so a silent engine is not measured as a cheap one.
    float outputPeak = 0.0f;

    /// The output's peak per hundred blocks, in order: whether the project kept sounding.
    std::vector<float> envelope;

    /// The output's RMS over the same stretches. What tells one passage from another where a
    /// master limiter holds every peak at its ceiling.
    std::vector<float> loudness;
};

class PumpThread final : public juce::Thread {
  public:
    /// @p speed is the pace as a multiple of real time.
    PumpThread(PumpBackend& backend, double sampleRate, int blockSize, double speed);
    ~PumpThread() override;

    /// Skip @p warmupSamples, then time callbacks for @p measuredSamples. Message thread.
    void measure(std::int64_t warmupSamples, std::int64_t measuredSamples);

    /// The span's result once it has ended, or nothing yet. Message thread.
    std::optional<SpanResult> result() const;

    void run() override;

  private:
    enum class Phase { Idle, Warmup, Measuring, Done };

    PumpBackend& backend_;
    const double sampleRate_;
    const int blockSize_;
    const double speed_;

    std::atomic<Phase> phase_{Phase::Idle};
    std::int64_t warmupRemaining_ = 0;
    std::int64_t measuredRemaining_ = 0;
    std::unique_ptr<BlockTimes> times_;
    SpanResult span_;
    double cpuAtStart_ = 0.0;
};

}  // namespace magda::parity
