#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <chrono>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace magda {

/**
 * @brief Performance profiling utilities for periodic performance checks
 *
 * Usage:
 * 1. Audio thread timing: Use ScopedProfiler in critical paths
 * 2. UI responsiveness: Track frame times and event processing
 * 3. Plugin loading: Measure plugin scan and instantiation times
 * 4. Memory: Track allocations and peak usage
 */

// =========================================================================
// Timing Utilities
// =========================================================================

/**
 * @brief High-resolution timer for performance measurements
 */
class HighResTimer {
  public:
    HighResTimer() : start_(std::chrono::high_resolution_clock::now()) {}

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    double elapsedMilliseconds() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_).count();
    }

    double elapsedMicroseconds() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(now - start_).count();
    }

  private:
    std::chrono::high_resolution_clock::time_point start_;
};

/**
 * @brief RAII-style scoped profiler for automatic timing
 *
 * Example:
 * {
 *     ScopedProfiler prof("AudioCallback");
 *     // ... audio processing code ...
 * } // Automatically logs on destruction
 */
class ScopedProfiler {
  public:
    explicit ScopedProfiler(const juce::String& name, bool enabled = true)
        : name_(name), enabled_(enabled) {
        if (enabled_) {
            timer_.reset();
        }
    }

    ~ScopedProfiler() {
        if (enabled_) {
            double elapsed = timer_.elapsedMicroseconds();
            // Log to console if over threshold
            if (elapsed > 1000.0) {  // > 1ms
                DBG("[PROFILE] " << name_ << ": " << juce::String(elapsed / 1000.0, 2) << " ms");
            }
        }
    }

  private:
    juce::String name_;
    HighResTimer timer_;
    bool enabled_;
};

// =========================================================================
// Statistics Collection
// =========================================================================

/**
 * @brief Statistics for a series of measurements
 */
struct PerformanceStats {
    double min = std::numeric_limits<double>::max();
    double max = 0.0;
    double sum = 0.0;
    int count = 0;

    void addSample(double value) {
        min = std::min(min, value);
        max = std::max(max, value);
        sum += value;
        count++;
    }

    double average() const {
        return count > 0 ? sum / count : 0.0;
    }

    void reset() {
        min = std::numeric_limits<double>::max();
        max = 0.0;
        sum = 0.0;
        count = 0;
    }

    juce::String toString() const {
        return juce::String::formatted("avg: %.2f ms, min: %.2f ms, max: %.2f ms, samples: %d",
                                       average(), min, max, count);
    }
};

/**
 * @brief Central performance statistics collector
 */
class PerformanceMonitor {
  public:
    static PerformanceMonitor& getInstance() {
        static PerformanceMonitor instance;
        return instance;
    }

    /**
     * @brief Enable or disable profiling at runtime (debug builds only)
     * @param enabled true to enable profiling, false to disable
     */
    void setEnabled(bool enabled) {
        enabled_ = enabled;
    }

    /**
     * @brief Check if profiling is currently enabled
     */
    bool isEnabled() const {
        return enabled_;
    }

    /**
     * @brief Add a timing sample for a named operation
     */
    void addSample(const juce::String& category, double milliseconds) {
        if (!enabled_)
            return;

        const juce::ScopedLock lock(statsLock_);
        stats_[category].addSample(milliseconds);
    }

    /**
     * @brief Get statistics for a category
     */
    PerformanceStats getStats(const juce::String& category) const {
        const juce::ScopedLock lock(statsLock_);
        auto it = stats_.find(category);
        return it != stats_.end() ? it->second : PerformanceStats{};
    }

    /**
     * @brief Get all collected statistics
     */
    std::unordered_map<juce::String, PerformanceStats> getAllStats() const {
        const juce::ScopedLock lock(statsLock_);
        return stats_;
    }

    /**
     * @brief Reset statistics for a category
     */
    void reset(const juce::String& category) {
        const juce::ScopedLock lock(statsLock_);
        stats_[category].reset();
    }

    /**
     * @brief Reset all statistics
     */
    void resetAll() {
        const juce::ScopedLock lock(statsLock_);
        stats_.clear();
    }

    /**
     * @brief Shutdown and clear all resources
     * Call during app shutdown to prevent static cleanup issues
     */
    void shutdown() {
        const juce::ScopedLock lock(statsLock_);
        stats_.clear();
        enabled_ = false;
    }

    /**
     * @brief Generate a performance report
     */
    juce::String generateReport() const {
        const juce::ScopedLock lock(statsLock_);
        juce::String report;
        report << "=== Performance Report ===\n\n";

        for (const auto& [category, stats] : stats_) {
            report << category << ": " << stats.toString() << "\n";
        }

        return report;
    }

  private:
    PerformanceMonitor() = default;

    mutable juce::CriticalSection statsLock_;
    std::unordered_map<juce::String, PerformanceStats> stats_;
    bool enabled_ = false;  // Profiling disabled by default, enable with setEnabled(true)
};

/**
 * @brief RAII profiler that reports to PerformanceMonitor
 */
class MonitoredProfiler {
  public:
    explicit MonitoredProfiler(const juce::String& category) : category_(category), timer_() {}

    ~MonitoredProfiler() {
        try {
            double elapsed = timer_.elapsedMilliseconds();
            PerformanceMonitor::getInstance().addSample(category_, elapsed);
        } catch (const std::exception& e) {
            juce::Logger::writeToLog(juce::String("[MonitoredProfiler] ") + e.what());
        } catch (...) {
            juce::Logger::writeToLog("[MonitoredProfiler] unknown exception");
        }
    }

  private:
    juce::String category_;
    HighResTimer timer_;
};

// =========================================================================
// Macros for Convenience
// =========================================================================

#if JUCE_DEBUG
    #define MAGDA_PROFILE_SCOPE(name) ScopedProfiler _profiler_##__LINE__(name)
    #define MAGDA_PROFILE_FUNCTION() ScopedProfiler _profiler_##__LINE__(__FUNCTION__)
    #define MAGDA_MONITOR_SCOPE(category) MonitoredProfiler _monitor_##__LINE__(category)
#else
    #define MAGDA_PROFILE_SCOPE(name)                                                              \
        do {                                                                                       \
        } while (false)
    #define MAGDA_PROFILE_FUNCTION()                                                               \
        do {                                                                                       \
        } while (false)
    #define MAGDA_MONITOR_SCOPE(category)                                                          \
        do {                                                                                       \
        } while (false)
#endif

}  // namespace magda
