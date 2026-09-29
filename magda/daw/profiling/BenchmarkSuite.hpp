#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>

#include "PerformanceProfiler.hpp"

namespace magda {

/**
 * @brief Comprehensive benchmark suite for periodic performance testing
 *
 * Tests:
 * 1. Audio thread performance (buffer processing time)
 * 2. UI responsiveness (frame times, event latency)
 * 3. Plugin loading times
 * 4. Memory usage patterns
 * 5. MIDI processing latency
 */
class BenchmarkSuite {
  public:
    struct BenchmarkResults {
        // Audio Thread Performance
        double audioCallbackAvg = 0.0;  // milliseconds
        double audioCallbackMax = 0.0;  // milliseconds
        int audioCallbackOverruns = 0;  // times exceeded buffer duration
        double cpuUsagePercent = 0.0;   // CPU usage %

        // UI Performance
        double uiFrameTimeAvg = 0.0;  // milliseconds
        double uiFrameTimeMax = 0.0;  // milliseconds
        int droppedFrames = 0;        // frames > 16.67ms (60 FPS target)

        // Plugin Performance
        double pluginScanTimeAvg = 0.0;  // milliseconds per plugin
        double pluginLoadTimeAvg = 0.0;  // milliseconds per plugin
        int pluginLoadFailures = 0;

        // Memory
        size_t peakMemoryMB = 0;     // peak memory usage
        size_t currentMemoryMB = 0;  // current memory usage

        // MIDI
        double midiLatencyAvg = 0.0;  // milliseconds (input to processing)

        juce::String toFormattedString() const {
            juce::String s;
            s << "=== MAGDA Performance Benchmark Results ===\n\n";

            s << "Audio Thread:\n";
            s << "  Avg callback time: " << juce::String(audioCallbackAvg, 3) << " ms\n";
            s << "  Max callback time: " << juce::String(audioCallbackMax, 3) << " ms\n";
            s << "  Buffer overruns: " << audioCallbackOverruns << "\n";
            s << "  CPU usage: " << juce::String(cpuUsagePercent, 1) << " %\n\n";

            s << "UI Rendering:\n";
            s << "  Avg frame time: " << juce::String(uiFrameTimeAvg, 2) << " ms\n";
            s << "  Max frame time: " << juce::String(uiFrameTimeMax, 2) << " ms\n";
            s << "  Dropped frames: " << droppedFrames << "\n\n";

            s << "Plugin Performance:\n";
            s << "  Avg scan time: " << juce::String(pluginScanTimeAvg, 2) << " ms\n";
            s << "  Avg load time: " << juce::String(pluginLoadTimeAvg, 2) << " ms\n";
            s << "  Load failures: " << pluginLoadFailures << "\n\n";

            s << "Memory:\n";
            s << "  Current: " << currentMemoryMB << " MB\n";
            s << "  Peak: " << peakMemoryMB << " MB\n\n";

            s << "MIDI:\n";
            s << "  Avg latency: " << juce::String(midiLatencyAvg, 2) << " ms\n";

            return s;
        }
    };

    BenchmarkSuite() = default;

    /**
     * @brief Run all benchmarks
     * @param sampleDurationSeconds How long to collect samples
     * @return Benchmark results
     */
    BenchmarkResults runAll(double sampleDurationSeconds = 10.0) {
        BenchmarkResults results;

        DBG("[BENCHMARK] Starting comprehensive benchmark (duration: " << sampleDurationSeconds
                                                                       << "s)");

        // Collect stats from PerformanceMonitor
        results = collectMonitorStats();

        // Run specific benchmarks
        results.currentMemoryMB = getCurrentMemoryUsageMB();
        results.peakMemoryMB = getPeakMemoryUsageMB();

        DBG("[BENCHMARK] Benchmark complete");
        DBG(results.toFormattedString());

        return results;
    }

    /**
     * @brief Start continuous monitoring (call this at app startup)
     */
    void startContinuousMonitoring() {
        PerformanceMonitor::getInstance().resetAll();
        DBG("[BENCHMARK] Continuous monitoring started");
    }

    /**
     * @brief Stop continuous monitoring and generate report
     */
    BenchmarkResults stopContinuousMonitoring() {
        auto results = collectMonitorStats();
        PerformanceMonitor::getInstance().resetAll();
        DBG("[BENCHMARK] Continuous monitoring stopped");
        return results;
    }

    /**
     * @brief Save benchmark results to file
     */
    bool saveBenchmarkResults(const BenchmarkResults& results, const juce::File& outputFile) {
        juce::String csvHeader =
            "Timestamp,AudioCallbackAvg,AudioCallbackMax,AudioOverruns,CPUUsage,"
            "UIFrameAvg,UIFrameMax,DroppedFrames,"
            "PluginScanAvg,PluginLoadAvg,PluginFailures,"
            "CurrentMemMB,PeakMemMB,MIDILatency\n";

        juce::String csvLine = juce::String::formatted(
            "%s,%.3f,%.3f,%d,%.1f,%.2f,%.2f,%d,%.2f,%.2f,%d,%zu,%zu,%.2f\n",
            juce::Time::getCurrentTime().toString(true, true).toRawUTF8(), results.audioCallbackAvg,
            results.audioCallbackMax, results.audioCallbackOverruns, results.cpuUsagePercent,
            results.uiFrameTimeAvg, results.uiFrameTimeMax, results.droppedFrames,
            results.pluginScanTimeAvg, results.pluginLoadTimeAvg, results.pluginLoadFailures,
            results.currentMemoryMB, results.peakMemoryMB, results.midiLatencyAvg);

        // Append to CSV file
        if (!outputFile.existsAsFile()) {
            outputFile.replaceWithText(csvHeader + csvLine);
        } else {
            outputFile.appendText(csvLine);
        }

        return true;
    }

  private:
    BenchmarkResults collectMonitorStats() const {
        BenchmarkResults results;
        auto& monitor = PerformanceMonitor::getInstance();

        // Audio callback stats
        auto audioStats = monitor.getStats("AudioCallback");
        results.audioCallbackAvg = audioStats.average();
        results.audioCallbackMax = audioStats.max;

        // UI frame stats
        auto uiStats = monitor.getStats("UIFrame");
        results.uiFrameTimeAvg = uiStats.average();
        results.uiFrameTimeMax = uiStats.max;
        // Count frames > 16.67ms (60 FPS)
        results.droppedFrames = 0;  // Would need to track this separately

        // Plugin stats
        auto pluginScanStats = monitor.getStats("PluginScan");
        results.pluginScanTimeAvg = pluginScanStats.average();

        auto pluginLoadStats = monitor.getStats("PluginLoad");
        results.pluginLoadTimeAvg = pluginLoadStats.average();

        // MIDI stats
        auto midiStats = monitor.getStats("MIDIProcessing");
        results.midiLatencyAvg = midiStats.average();

        return results;
    }

    size_t getCurrentMemoryUsageMB() const {
        // JUCE memory stats
        auto stats = juce::SystemStats::getMemoryUsageStats();
        return stats.totalMemoryInBytes / (1024 * 1024);
    }

    size_t getPeakMemoryUsageMB() const {
        auto stats = juce::SystemStats::getMemoryUsageStats();
        return stats.peakMemoryInBytes / (1024 * 1024);
    }
};

/**
 * @brief Automated periodic benchmark runner
 */
class PeriodicBenchmarkRunner : public juce::Timer {
  public:
    PeriodicBenchmarkRunner(const juce::File& outputDir) : outputDir_(outputDir) {
        outputDir_.createDirectory();
    }

    ~PeriodicBenchmarkRunner() {
        stopTimer();
    }

    /**
     * @brief Start running benchmarks periodically
     * @param intervalMinutes How often to run benchmarks
     */
    void start(int intervalMinutes = 30) {
        suite_.startContinuousMonitoring();
        startTimer(intervalMinutes * 60 * 1000);
        DBG("[BENCHMARK] Periodic runner started (interval: " << intervalMinutes << "min)");
    }

    void stop() {
        stopTimer();
        DBG("[BENCHMARK] Periodic runner stopped");
    }

  private:
    void timerCallback() override {
        DBG("[BENCHMARK] Running periodic benchmark...");

        auto results = suite_.stopContinuousMonitoring();

        // Save to timestamped file
        auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        auto outputFile = outputDir_.getChildFile("benchmark_" + timestamp + ".csv");

        suite_.saveBenchmarkResults(results, outputFile);

        // Restart monitoring for next interval
        suite_.startContinuousMonitoring();

        DBG("[BENCHMARK] Results saved to: " << outputFile.getFullPathName());
    }

    juce::File outputDir_;
    BenchmarkSuite suite_;
};

}  // namespace magda
