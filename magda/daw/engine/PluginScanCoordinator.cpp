#include "PluginScanCoordinator.hpp"

#include <iostream>

#include "core/Config.hpp"

namespace magda {

PluginScanCoordinator::PluginScanCoordinator() {
    loadExclusions();
}

PluginScanCoordinator::~PluginScanCoordinator() {
    isScanning_ = false;
    stopTimer();
    workers_.clear();
}

juce::File PluginScanCoordinator::getScannerExecutable() const {
    auto appBundle = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

#if JUCE_MAC
    auto scanner = appBundle.getChildFile("Contents/MacOS/magda_plugin_scanner");
    if (scanner.existsAsFile())
        return scanner;

    scanner = appBundle.getParentDirectory().getChildFile("magda_plugin_scanner");
    if (scanner.existsAsFile())
        return scanner;
#elif JUCE_WINDOWS
    auto scanner = appBundle.getParentDirectory().getChildFile("magda_plugin_scanner.exe");
    if (scanner.existsAsFile())
        return scanner;
#else
    auto scanner = appBundle.getParentDirectory().getChildFile("magda_plugin_scanner");
    if (scanner.existsAsFile())
        return scanner;
#endif

    std::cerr << "[ScanCoordinator] Scanner executable not found!" << std::endl;
    return {};
}

void PluginScanCoordinator::startScan(juce::AudioPluginFormatManager& formatManager,
                                      const ProgressCallback& progressCallback,
                                      const CompletionCallback& completionCallback) {
    if (isScanning_) {
        std::cout << "[ScanCoordinator] Scan already in progress" << std::endl;
        return;
    }

    formatManager_ = &formatManager;
    progressCallback_ = progressCallback;
    completionCallback_ = completionCallback;
    foundPlugins_.clear();
    failedPlugins_.clear();
    pluginsToScan_.clear();
    nextPluginIndex_ = 0;
    completedCount_ = 0;
    workers_.clear();
    workerStartTimes_.fill(0);
    workerCurrentPlugin_.fill({});
    workerCurrentFormat_.fill({});
    scanStartTime_ = juce::Time::currentTimeMillis();
    scanResults_.clear();

    // Discover all plugins to scan
    discoverPlugins();

    if (pluginsToScan_.empty()) {
        std::cout << "[ScanCoordinator] No plugins to scan" << std::endl;
        if (completionCallback)
            completionCallback(true, foundPlugins_, failedPlugins_);
        return;
    }

    std::cout << "[ScanCoordinator] Found " << pluginsToScan_.size() << " plugins to scan"
              << std::endl;

    auto scannerExe = getScannerExecutable();
    if (!scannerExe.existsAsFile()) {
        std::cerr << "[ScanCoordinator] Plugin scanner executable not found" << std::endl;
        if (completionCallback)
            completionCallback(false, foundPlugins_, failedPlugins_);
        return;
    }

    isScanning_ = true;

    // Create worker pool
    int numWorkers = std::min(NUM_WORKERS, static_cast<int>(pluginsToScan_.size()));
    for (int i = 0; i < numWorkers; ++i) {
        workers_.push_back(std::make_unique<ScanWorker>(
            i, scannerExe, [this](int workerIndex, const ScanWorker::Result& result) {
                onWorkerResult(workerIndex, result);
            }));
    }

    // Assign first batch of plugins to workers
    for (int i = 0; i < numWorkers; ++i) {
        assignNextPlugin(i);
    }

    // Start timeout timer
    startTimer(1000);
}

void PluginScanCoordinator::discoverPlugins() {
    if (!formatManager_)
        return;

    for (int i = 0; i < formatManager_->getNumFormats(); ++i) {
        auto* format = formatManager_->getFormat(i);
        if (!format)
            continue;

        juce::String formatName = format->getName();
        if (!formatName.containsIgnoreCase("VST3") && !formatName.containsIgnoreCase("AudioUnit"))
            continue;

        std::cout << "[ScanCoordinator] Discovering plugins for format: " << formatName
                  << std::endl;

        auto searchPath = format->getDefaultLocationsToSearch();

        // Append custom plugin directories from config
        for (const auto& p : Config::getInstance().getCustomPluginPaths())
            searchPath.add(juce::File(p));

        auto files = format->searchPathsForPlugins(searchPath, true, false);

        // Build excluded paths set for quick lookup
        juce::StringArray excludedPaths;
        for (const auto& entry : excludedPlugins_)
            excludedPaths.add(entry.path);

        for (const auto& file : files) {
            if (!excludedPaths.contains(file)) {
                pluginsToScan_.push_back({formatName, file});
            } else {
                std::cout << "[ScanCoordinator] Skipping excluded: " << file << std::endl;
            }
        }

        std::cout << "[ScanCoordinator] Found " << files.size() << " " << formatName << " plugins ("
                  << excludedPaths.size() << " excluded)" << std::endl;
    }
}

void PluginScanCoordinator::assignNextPlugin(int workerIndex) {
    if (nextPluginIndex_ >= static_cast<int>(pluginsToScan_.size()))
        return;

    auto& plugin = pluginsToScan_[static_cast<size_t>(nextPluginIndex_)];
    nextPluginIndex_++;

    std::cout << "[ScanCoordinator] Assigning to worker " << workerIndex << ": "
              << plugin.pluginPath << " (" << completedCount_ + 1 << "/" << pluginsToScan_.size()
              << ")" << std::endl;

    if (progressCallback_) {
        float progress =
            static_cast<float>(completedCount_) / static_cast<float>(pluginsToScan_.size());
        progressCallback_(progress, plugin.pluginPath);
    }

    workerStartTimes_[static_cast<size_t>(workerIndex)] = juce::Time::currentTimeMillis();
    workerCurrentPlugin_[static_cast<size_t>(workerIndex)] = plugin.pluginPath;
    workerCurrentFormat_[static_cast<size_t>(workerIndex)] = plugin.formatName;
    workers_[static_cast<size_t>(workerIndex)]->scanPlugin(plugin.formatName, plugin.pluginPath);
}

void PluginScanCoordinator::onWorkerResult(int workerIndex, const ScanWorker::Result& result) {
    if (!isScanning_)
        return;

    completedCount_++;

    // Record scan result
    {
        PluginScanResult scanResult;
        scanResult.pluginPath = result.pluginPath;
        scanResult.formatName = workerCurrentFormat_[static_cast<size_t>(workerIndex)];
        scanResult.success = result.success;
        scanResult.errorMessage = result.errorMessage;
        scanResult.durationMs =
            juce::Time::currentTimeMillis() - workerStartTimes_[static_cast<size_t>(workerIndex)];
        scanResult.workerIndex = workerIndex;
        for (const auto& desc : result.foundPlugins)
            scanResult.pluginNames.add(desc.name);
        scanResults_.push_back(scanResult);
    }

    if (result.success) {
        for (const auto& desc : result.foundPlugins)
            foundPlugins_.add(desc);
    } else {
        failedPlugins_.add(result.pluginPath);
        excludePlugin(result.pluginPath,
                      result.errorMessage.isNotEmpty() ? result.errorMessage : "unknown");
        std::cout << "[ScanCoordinator] Failed: " << result.pluginPath << " - "
                  << result.errorMessage << std::endl;
    }

    // Report progress
    if (progressCallback_) {
        float progress =
            static_cast<float>(completedCount_) / static_cast<float>(pluginsToScan_.size());
        juce::String currentPlugin = result.pluginPath;
        progressCallback_(progress, currentPlugin);
    }

    // Assign next plugin or check if done
    workerStartTimes_[static_cast<size_t>(workerIndex)] = 0;
    workerCurrentPlugin_[static_cast<size_t>(workerIndex)] = {};
    if (nextPluginIndex_ < static_cast<int>(pluginsToScan_.size())) {
        assignNextPlugin(workerIndex);
    } else {
        checkIfAllDone();
    }
}

void PluginScanCoordinator::checkIfAllDone() {
    for (const auto& worker : workers_) {
        if (worker->isBusy())
            return;
    }

    finishScan(true);
}

void PluginScanCoordinator::timerCallback() {
    if (!isScanning_) {
        stopTimer();
        return;
    }

    juce::int64 now = juce::Time::currentTimeMillis();

    for (size_t i = 0; i < workers_.size(); ++i) {
        if (!workers_[i]->isBusy() || workerStartTimes_[i] == 0)
            continue;

        juce::int64 elapsed = now - workerStartTimes_[i];
        if (elapsed > pluginTimeoutMs_) {
            juce::String timedOutPlugin = workerCurrentPlugin_[i];
            std::cout << "[ScanCoordinator] Worker " << i << " timed out on: " << timedOutPlugin
                      << std::endl;

            // Abort kills the subprocess (sets busy_=false first, so handleConnectionLost
            // won't fire a result). We handle the timeout result here manually.
            workers_[i]->abort();

            // Record timeout result
            {
                PluginScanResult scanResult;
                scanResult.pluginPath = timedOutPlugin;
                scanResult.formatName = workerCurrentFormat_[i];
                scanResult.success = false;
                scanResult.errorMessage =
                    "timeout (" + juce::String(pluginTimeoutMs_ / 1000) + "s)";
                scanResult.durationMs = elapsed;
                scanResult.workerIndex = static_cast<int>(i);
                scanResults_.push_back(scanResult);
            }

            if (timedOutPlugin.isNotEmpty()) {
                excludePlugin(timedOutPlugin, "timeout");
                failedPlugins_.add(timedOutPlugin);
            }

            completedCount_++;
            workerStartTimes_[i] = 0;
            workerCurrentPlugin_[i] = {};

            // Report progress
            if (progressCallback_) {
                float progress =
                    static_cast<float>(completedCount_) / static_cast<float>(pluginsToScan_.size());
                progressCallback_(progress, timedOutPlugin + " (timed out)");
            }

            // Assign next plugin or check if done
            if (nextPluginIndex_ < static_cast<int>(pluginsToScan_.size())) {
                assignNextPlugin(static_cast<int>(i));
            } else {
                checkIfAllDone();
            }
        }
    }
}

void PluginScanCoordinator::abortScan() {
    isScanning_ = false;
    stopTimer();

    for (auto& worker : workers_)
        worker->abort();
    workers_.clear();
}

void PluginScanCoordinator::finishScan(bool success) {
    std::cout << "[ScanCoordinator] finishScan called, success=" << success << std::endl;

    isScanning_ = false;
    stopTimer();
    workers_.clear();

    writeScanReport();

    std::cout << "[ScanCoordinator] Scan finished. Found " << foundPlugins_.size() << " plugins, "
              << failedPlugins_.size() << " failed." << std::endl;

    auto callback = completionCallback_;
    completionCallback_ = nullptr;

    if (callback)
        callback(success, foundPlugins_, failedPlugins_);
}

// Exclusion management
juce::File PluginScanCoordinator::getExclusionFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("plugin_exclusions.txt");
}

const std::vector<ExcludedPlugin>& PluginScanCoordinator::getExcludedPlugins() const {
    return excludedPlugins_;
}

void PluginScanCoordinator::clearExclusions() {
    excludedPlugins_.clear();
    saveExclusions();
}

void PluginScanCoordinator::excludePlugin(const juce::String& pluginPath,
                                          const juce::String& reason) {
    for (const auto& entry : excludedPlugins_) {
        if (entry.path == pluginPath)
            return;
    }

    ExcludedPlugin entry;
    entry.path = pluginPath;
    entry.reason = reason;
    entry.timestamp = juce::Time::getCurrentTime().toISO8601(true);
    excludedPlugins_.push_back(entry);
    saveExclusions();
}

void PluginScanCoordinator::loadExclusions() {
    excludedPlugins_ = loadExclusionList(getExclusionFile());
    std::cout << "[ScanCoordinator] Loaded " << excludedPlugins_.size() << " excluded plugins"
              << std::endl;
}

void PluginScanCoordinator::saveExclusions() {
    saveExclusionList(getExclusionFile(), excludedPlugins_);
}

juce::File PluginScanCoordinator::getScanReportFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("last_scan_report.txt");
}

void PluginScanCoordinator::writeScanReport() {
    auto reportFile = getScanReportFile();
    reportFile.getParentDirectory().createDirectory();

    juce::int64 totalDurationMs = juce::Time::currentTimeMillis() - scanStartTime_;
    double totalDurationSec = static_cast<double>(totalDurationMs) / 1000.0;

    int successCount = 0;
    int failCount = 0;
    int totalPluginsFound = 0;
    for (const auto& r : scanResults_) {
        if (r.success) {
            successCount++;
            totalPluginsFound += r.pluginNames.size();
        } else {
            failCount++;
        }
    }

    juce::String report;
    report << "=== MAGDA Plugin Scan Report ===" << juce::newLine;
    report << "Date: " << juce::Time::getCurrentTime().toString(true, true) << juce::newLine;
    report << "Duration: " << juce::String(totalDurationSec, 1) << "s" << juce::newLine;
    report << "Workers: " << static_cast<int>(workers_.size() > 0 ? workers_.size() : NUM_WORKERS)
           << juce::newLine;
    report << "Plugins scanned: " << static_cast<int>(scanResults_.size()) << juce::newLine;
    report << "Succeeded: " << successCount << " (found " << totalPluginsFound << " plugins)"
           << juce::newLine;
    report << "Failed: " << failCount << juce::newLine;
    report << juce::newLine;

    // Failed plugins section
    if (failCount > 0) {
        report << "--- Failed Plugins ---" << juce::newLine;
        for (const auto& r : scanResults_) {
            if (r.success)
                continue;

            juce::String tag;
            if (r.errorMessage.containsIgnoreCase("timeout"))
                tag = "TIMEOUT";
            else if (r.errorMessage.containsIgnoreCase("crash"))
                tag = "CRASH";
            else
                tag = "ERROR";

            report << "[" << tag << "] " << r.pluginPath;
            if (r.errorMessage.isNotEmpty() && tag == "ERROR")
                report << " - " << r.errorMessage;
            report << " (worker " << r.workerIndex << ", "
                   << juce::String(static_cast<double>(r.durationMs) / 1000.0, 1) << "s)"
                   << juce::newLine;
        }
        report << juce::newLine;
    }

    // All results section
    report << "--- All Results ---" << juce::newLine;
    for (const auto& r : scanResults_) {
        if (r.success) {
            juce::String names = r.pluginNames.joinIntoString(", ");
            report << "[OK]      " << names << " (" << r.formatName << ") - " << r.pluginPath
                   << " (worker " << r.workerIndex << ", "
                   << juce::String(static_cast<double>(r.durationMs) / 1000.0, 1) << "s)"
                   << juce::newLine;
        } else {
            juce::String tag;
            if (r.errorMessage.containsIgnoreCase("timeout"))
                tag = "TIMEOUT";
            else if (r.errorMessage.containsIgnoreCase("crash"))
                tag = "CRASH";
            else
                tag = "ERROR";

            report << "[" << tag << "]   " << r.pluginPath;
            if (r.errorMessage.isNotEmpty() && tag == "ERROR")
                report << " - " << r.errorMessage;
            report << " (worker " << r.workerIndex << ", "
                   << juce::String(static_cast<double>(r.durationMs) / 1000.0, 1) << "s)"
                   << juce::newLine;
        }
    }

    reportFile.replaceWithText(report);
    std::cout << "[ScanCoordinator] Scan report written to: " << reportFile.getFullPathName()
              << std::endl;
}

}  // namespace magda
