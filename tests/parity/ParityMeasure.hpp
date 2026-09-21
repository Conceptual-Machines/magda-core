#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

/**
 * @file ParityMeasure.hpp
 * @brief The engine-agnostic instruments of the parity envelope bench (#2082).
 */

namespace magda::parity {

/// Callback wall times over a run, in microseconds.
struct BlockTimeSummary {
    int blocks = 0;
    double meanUs = 0.0;
    double p50Us = 0.0;
    double p95Us = 0.0;
    double p99Us = 0.0;
    double maxUs = 0.0;
};

/** @brief Collects one duration per callback. Reserved up front so recording never allocates. */
class BlockTimes {
  public:
    explicit BlockTimes(std::size_t expectedBlocks) {
        micros_.reserve(expectedBlocks);
    }

    void record(std::chrono::steady_clock::duration elapsed) {
        if (micros_.size() < micros_.capacity())
            micros_.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
    }

    BlockTimeSummary summary() const;

  private:
    std::vector<double> micros_;
};

/// CPU time this process has used on every thread, user and system, in seconds.
double processCpuSeconds();

/// What this process holds in physical memory now: the phys footprint on macOS, the resident set
/// on Linux, the private commit on Windows.
std::int64_t currentFootprintBytes();

/** @brief Polls the footprint on its own thread and keeps the highest reading. */
class FootprintSampler {
  public:
    explicit FootprintSampler(std::chrono::milliseconds interval = std::chrono::milliseconds(5));
    ~FootprintSampler();

    FootprintSampler(const FootprintSampler&) = delete;
    FootprintSampler& operator=(const FootprintSampler&) = delete;

    /// Stops polling and returns the highest footprint seen, including one read taken now.
    std::int64_t stop();

  private:
    std::atomic<bool> running_{true};
    std::atomic<std::int64_t> peak_{0};
    std::thread thread_;
};

}  // namespace magda::parity
