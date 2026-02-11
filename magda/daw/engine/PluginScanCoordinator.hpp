#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "PluginExclusions.hpp"
#include "ScanWorker.hpp"

namespace magda {

struct PluginScanResult {
    juce::String pluginPath;
    juce::String formatName;
    bool success = false;
    juce::String errorMessage;
    juce::int64 durationMs = 0;
    int workerIndex = -1;
    juce::StringArray pluginNames;
};

class PluginScanCoordinator : private juce::Timer {
  public:
    PluginScanCoordinator();
    ~PluginScanCoordinator() override;

    using ProgressCallback = std::function<void(float progress, const juce::String& currentPlugin)>;

    using CompletionCallback =
        std::function<void(bool success, const juce::Array<juce::PluginDescription>& plugins,
                           const juce::StringArray& failedPlugins)>;

    void startScan(juce::AudioPluginFormatManager& formatManager,
                   const ProgressCallback& progressCallback,
                   const CompletionCallback& completionCallback);

    void abortScan();

    bool isScanning() const {
        return isScanning_;
    }

    const juce::Array<juce::PluginDescription>& getFoundPlugins() const {
        return foundPlugins_;
    }

    const std::vector<ExcludedPlugin>& getExcludedPlugins() const;

    void clearExclusions();

    void excludePlugin(const juce::String& pluginPath, const juce::String& reason = "unknown");

    void setPluginTimeoutMs(int timeoutMs) {
        pluginTimeoutMs_ = timeoutMs;
    }
    int getPluginTimeoutMs() const {
        return pluginTimeoutMs_;
    }

    juce::File getScanReportFile() const;

  private:
    static constexpr int NUM_WORKERS = 4;
    static constexpr int DEFAULT_PLUGIN_TIMEOUT_MS = 120000;
    int pluginTimeoutMs_ = DEFAULT_PLUGIN_TIMEOUT_MS;

    // Timer for timeout detection
    void timerCallback() override;

    // Discovery
    void discoverPlugins();

    // Work distribution
    void assignNextPlugin(int workerIndex);
    void onWorkerResult(int workerIndex, const ScanWorker::Result& result);
    void checkIfAllDone();
    void finishScan(bool success);
    void writeScanReport();

    // Find the scanner executable
    juce::File getScannerExecutable() const;

    // Exclusion management
    juce::File getExclusionFile() const;
    void loadExclusions();
    void saveExclusions();

    // State
    bool isScanning_ = false;
    juce::AudioPluginFormatManager* formatManager_ = nullptr;
    ProgressCallback progressCallback_;
    CompletionCallback completionCallback_;

    // Worker pool
    std::vector<std::unique_ptr<ScanWorker>> workers_;

    // Plugin queue
    struct PluginToScan {
        juce::String formatName;
        juce::String pluginPath;
    };
    std::vector<PluginToScan> pluginsToScan_;
    int nextPluginIndex_ = 0;
    int completedCount_ = 0;

    // Timeout tracking per worker
    std::array<juce::int64, NUM_WORKERS> workerStartTimes_{};
    std::array<juce::String, NUM_WORKERS> workerCurrentPlugin_;
    std::array<juce::String, NUM_WORKERS> workerCurrentFormat_;

    // Scan report
    juce::int64 scanStartTime_ = 0;
    std::vector<PluginScanResult> scanResults_;

    // Results
    juce::Array<juce::PluginDescription> foundPlugins_;
    juce::StringArray failedPlugins_;
    std::vector<ExcludedPlugin> excludedPlugins_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginScanCoordinator)
};

}  // namespace magda
