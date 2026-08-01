#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "PluginExclusions.hpp"
#include "ScanWorker.hpp"

namespace magda {

/**
 * @brief Short display label for whatever identifies a plugin.
 *
 * VST3 and AudioUnit identify a plugin by absolute path, so the file name is
 * the useful part. LV2 identifies one by URI, e.g.
 * "http://gareus.org/oss/lv2/midifilter#cctonote". That is not a path, so
 * passing it to juce::File trips the "Illegal absolute path" assertion; take
 * the fragment or last segment instead.
 */
inline juce::String pluginDisplayName(const juce::String& identifier) {
    if (identifier.isEmpty())
        return identifier;

    if (juce::File::isAbsolutePath(identifier)) {
        const auto name = juce::File(identifier).getFileName();
        return name.isNotEmpty() ? name : identifier;
    }

    if (identifier.contains("#"))
        return identifier.fromLastOccurrenceOf("#", false, false);
    if (identifier.contains("/"))
        return identifier.fromLastOccurrenceOf("/", false, false);
    return identifier;
}

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
    struct PluginToScan {
        juce::String formatName;
        juce::String pluginPath;
    };

    PluginScanCoordinator();
    ~PluginScanCoordinator() override;

    using ProgressCallback = std::function<void(float progress, const juce::String& currentPlugin)>;

    using CompletionCallback =
        std::function<void(bool success, const juce::Array<juce::PluginDescription>& plugins,
                           const juce::StringArray& failedPlugins)>;

    void startScan(juce::AudioPluginFormatManager& formatManager,
                   const ProgressCallback& progressCallback,
                   const CompletionCallback& completionCallback);

    /** Scan only specific plugins (for incremental/diff scanning). */
    void startIncrementalScan(juce::AudioPluginFormatManager& formatManager,
                              const std::vector<PluginToScan>& plugins,
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

    /** Discover all plugin files on disk (respecting exclusions), without scanning them. */
    std::vector<PluginToScan> discoverPluginFiles(juce::AudioPluginFormatManager& formatManager);

    /** Thread-safe overload using pre-snapshotted exclusion and custom path data. */
    static std::vector<PluginToScan> discoverPluginFiles(
        juce::AudioPluginFormatManager& formatManager, const juce::StringArray& excludedPaths,
        const std::vector<std::string>& customPaths);

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

    // Orphan process cleanup
    void killOrphanScannerProcesses();

    // Exclusion management
    void loadExclusions();
    void saveExclusions();
    bool exclusionsLoaded_ = false;

    // State
    std::atomic<bool> isScanning_{false};
    juce::AudioPluginFormatManager* formatManager_ = nullptr;
    ProgressCallback progressCallback_;
    CompletionCallback completionCallback_;

    // Worker pool
    std::vector<std::unique_ptr<ScanWorker>> workers_;

    // Plugin queue
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
