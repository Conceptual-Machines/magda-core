#include "PluginScanner.hpp"

namespace magda {

PluginScanner::PluginScanner() : juce::Thread("Plugin Scanner") {
    loadExclusions();
}

PluginScanner::~PluginScanner() {
    abortScan();
}

void PluginScanner::startScan(juce::AudioPluginFormatManager& formatManager,
                              const ProgressCallback& progressCallback,
                              const CompletionCallback& completionCallback) {
    if (isThreadRunning()) {
        DBG("Scan already in progress");
        return;
    }

    formatManager_ = &formatManager;
    progressCallback_ = progressCallback;
    completionCallback_ = completionCallback;
    foundPlugins_.clear();
    failedPlugins_.clear();

    startThread();
}

void PluginScanner::abortScan() {
    signalThreadShouldExit();
    stopThread(5000);
}

void PluginScanner::run() {
    DBG("Plugin scan started on background thread");

    if (!formatManager_) {
        DBG("No format manager set");
        return;
    }

    // Extract paths for the scanning exclusion check
    juce::StringArray excluded;
    for (const auto& entry : excludedPlugins_)
        excluded.add(entry.path);
    juce::KnownPluginList tempKnownList;

    // Scan each format
    for (int i = 0; i < formatManager_->getNumFormats() && !threadShouldExit(); ++i) {
        auto* format = formatManager_->getFormat(i);
        if (!format)
            continue;

        juce::String formatName = format->getName();

        // Only scan VST3 and AudioUnit
        if (!formatName.containsIgnoreCase("VST3") && !formatName.containsIgnoreCase("AudioUnit")) {
            continue;
        }

        DBG("Scanning format: " << formatName);

        // Report starting this format
        if (progressCallback_) {
            juce::MessageManager::callAsync([this, formatName]() {
                if (progressCallback_) {
                    progressCallback_(0.0f, "Starting " + formatName + " scan...");
                }
            });
        }

        auto searchPath = format->getDefaultLocationsToSearch();

        // Dead man's pedal - if we crash, this file tells us which plugin was being scanned
        juce::File deadMansPedal =
            juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("MAGDA")
                .getChildFile("scanning_" + formatName + ".txt");
        (void)deadMansPedal.getParentDirectory().createDirectory();

        // Check if there's a dead man's pedal from a previous crash
        if (deadMansPedal.existsAsFile()) {
            juce::String crashedPlugin = deadMansPedal.loadFileAsString().trim();
            if (crashedPlugin.isNotEmpty() && !excluded.contains(crashedPlugin)) {
                DBG("Previous crash detected on: " << crashedPlugin);
                excludePlugin(crashedPlugin, "crash");
                excluded.add(crashedPlugin);
            }
        }

        tempKnownList.clear();

        juce::PluginDirectoryScanner scanner(tempKnownList, *format, searchPath, true,
                                             deadMansPedal, false);

        juce::String nextPlugin;
        int scanned = 0;

        while (scanner.scanNextFile(true, nextPlugin) && !threadShouldExit()) {
            // Skip excluded plugins
            if (excluded.contains(nextPlugin)) {
                DBG("Skipping excluded: " << nextPlugin);
                continue;
            }

            scanned++;
            [[maybe_unused]] float progress = scanner.getProgress();

            // Report progress on message thread
            if (progressCallback_) {
                juce::MessageManager::callAsync([this, progress, nextPlugin]() {
                    if (progressCallback_) {
                        progressCallback_(progress, nextPlugin);
                    }
                });
            }
        }

        DBG("Scanned " << scanned << " " << formatName << " plugins");

        // Copy found plugins to our list
        for (const auto& desc : tempKnownList.getTypes()) {
            foundPlugins_.add(desc);
            DBG("Found: " << desc.name << " (" << desc.pluginFormatName << ")");
        }

        // Record failed plugins
        auto failed = scanner.getFailedFiles();
        for (const auto& failedFile : failed) {
            DBG("Failed: " << failedFile);
            failedPlugins_.add(failedFile);
            excludePlugin(failedFile, "scan_failed");
        }

        // Clean up dead man's pedal
        (void)deadMansPedal.deleteFile();
    }

    if (threadShouldExit()) {
        DBG("Plugin scan aborted");
        return;
    }

    DBG("Plugin scan complete. Found " << foundPlugins_.size() << " plugins, "
                                       << failedPlugins_.size() << " failed.");

    // Notify completion on message thread
    auto plugins = foundPlugins_;
    auto failed = failedPlugins_;
    auto callback = completionCallback_;

    juce::MessageManager::callAsync([callback, plugins, failed]() {
        if (callback) {
            callback(true, plugins, failed);
        }
    });
}

const std::vector<ExcludedPlugin>& PluginScanner::getExcludedPlugins() const {
    return excludedPlugins_;
}

void PluginScanner::clearExclusions() {
    excludedPlugins_.clear();
    saveExclusions();
}

void PluginScanner::excludePlugin(const juce::String& pluginPath, const juce::String& reason) {
    // Check if already excluded
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

juce::File PluginScanner::getExclusionFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("MAGDA")
        .getChildFile("plugin_exclusions.txt");
}

void PluginScanner::loadExclusions() {
    excludedPlugins_ = loadExclusionList(getExclusionFile());
    DBG("Loaded " << excludedPlugins_.size() << " excluded plugins");
}

void PluginScanner::saveExclusions() {
    saveExclusionList(getExclusionFile(), excludedPlugins_);
}

}  // namespace magda
