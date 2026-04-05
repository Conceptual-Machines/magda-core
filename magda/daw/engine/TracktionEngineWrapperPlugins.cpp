#include <thread>

#include "PluginScanCoordinator.hpp"
#include "TracktionEngineWrapper.hpp"
#include "core/Config.hpp"

namespace magda {

std::string TracktionEngineWrapper::addEffect(const std::string& track_id,
                                              const std::string& effect_name) {
    // TODO: Implement effect addition
    auto effectId = generateEffectId();
    DBG("Added effect (stub): " << effect_name << " to track " << track_id);
    return effectId;
}

void TracktionEngineWrapper::removeEffect(const std::string& effect_id) {
    // TODO: Implement effect removal
    DBG("Removed effect (stub): " << effect_id);
}

void TracktionEngineWrapper::setEffectParameter(const std::string& effect_id,
                                                const std::string& parameter_name, double value) {
    // TODO: Implement effect parameter setting
    DBG("Set effect parameter (stub): " << effect_id << "." << parameter_name << " = " << value);
}

double TracktionEngineWrapper::getEffectParameter(const std::string& effect_id,
                                                  const std::string& parameter_name) const {
    // TODO: Implement effect parameter retrieval
    return 0.0;
}

void TracktionEngineWrapper::setEffectEnabled(const std::string& effect_id, bool enabled) {
    // TODO: Implement effect enable/disable
    DBG("Set effect enabled (stub): " << effect_id << " = " << (int)enabled);
}

bool TracktionEngineWrapper::isEffectEnabled(const std::string& effect_id) const {
    // TODO: Implement effect enabled check
    return true;
}

std::vector<std::string> TracktionEngineWrapper::getAvailableEffects() const {
    // TODO: Implement available effects retrieval
    return {"Reverb", "Delay", "EQ", "Compressor"};
}

std::vector<std::string> TracktionEngineWrapper::getTrackEffects(
    const std::string& track_id) const {
    // TODO: Implement track effects retrieval
    return {};
}

// =============================================================================
// Plugin Scanning - Uses out-of-process scanner to prevent crashes
// =============================================================================

void TracktionEngineWrapper::startPluginScan(
    std::function<void(float, const juce::String&)> progressCallback) {
    if (!engine_ || isScanning_) {
        return;
    }

    isScanning_ = true;
    scanProgressCallback_ = progressCallback;

    auto& pluginManager = engine_->getPluginManager();
    auto& knownPlugins = pluginManager.knownPluginList;
    auto& formatManager = pluginManager.pluginFormatManager;

    // List available formats
    juce::StringArray formatNames;
    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
        auto* format = formatManager.getFormat(i);
        if (format) {
            formatNames.add(format->getName());
        }
    }
    DBG("Starting plugin scan with OUT-OF-PROCESS scanner");
    DBG("Available formats: " << formatNames.joinIntoString(", "));

    // Create coordinator if needed
    if (!pluginScanCoordinator_) {
        pluginScanCoordinator_ = std::make_unique<PluginScanCoordinator>();
    }

    // Start scanning using the out-of-process coordinator
    pluginScanCoordinator_->startScan(
        formatManager,
        // Progress callback
        [this, progressCallback](float progress, const juce::String& currentPlugin) {
            if (progressCallback) {
                progressCallback(progress, currentPlugin);
            }
        },
        // Completion callback
        [this, &knownPlugins](bool success, const juce::Array<juce::PluginDescription>& plugins,
                              const juce::StringArray& failedPlugins) {
            // Add found plugins to KnownPluginList
            for (const auto& desc : plugins) {
                knownPlugins.addType(desc);
            }

            // Remove stale plugins whose files no longer exist on disk
            {
                std::unordered_set<std::string> scannedPaths;
                for (const auto& desc : plugins)
                    scannedPaths.insert(desc.fileOrIdentifier.toStdString());

                juce::Array<juce::PluginDescription> stalePlugins;
                for (int i = 0; i < knownPlugins.getNumTypes(); ++i) {
                    auto* desc = knownPlugins.getType(i);
                    if (desc && scannedPaths.find(desc->fileOrIdentifier.toStdString()) ==
                                    scannedPaths.end()) {
                        juce::File pluginFile(desc->fileOrIdentifier);
                        if (juce::File::isAbsolutePath(desc->fileOrIdentifier) &&
                            !pluginFile.exists()) {
                            DBG("Removing stale plugin: " << desc->name << " ("
                                                          << desc->fileOrIdentifier << ")");
                            stalePlugins.add(*desc);
                        }
                    }
                }
                for (const auto& desc : stalePlugins)
                    knownPlugins.removeType(desc);

                if (!stalePlugins.isEmpty())
                    DBG("Removed " << stalePlugins.size() << " stale plugin(s)");
            }

            int numPlugins = knownPlugins.getNumTypes();
            DBG("Plugin scan complete. Found " << numPlugins << " plugins.");

            if (failedPlugins.size() > 0) {
                DBG("Failed/crashed plugins (" << failedPlugins.size() << "):");
                for (const auto& failed : failedPlugins) {
                    DBG("  - " << failed);
                }
            }

            // Save the updated plugin list to persistent storage
            savePluginList();

            // Persist total plugin count only after a successful scan
            if (success) {
                Config::getInstance().setTotalPluginCount(numPlugins);
                Config::getInstance().save();
            }

            isScanning_ = false;

            if (onPluginScanComplete) {
                onPluginScanComplete(success, numPlugins, failedPlugins);
            }
        });
}

void TracktionEngineWrapper::abortPluginScan() {
    if (pluginScanCoordinator_) {
        pluginScanCoordinator_->abortScan();
    }
    isScanning_ = false;
}

void TracktionEngineWrapper::clearPluginExclusions() {
    // Clear the exclusion list in the coordinator
    if (pluginScanCoordinator_) {
        pluginScanCoordinator_->clearExclusions();
    } else {
        // Create a temporary coordinator just to clear the exclusion list
        PluginScanCoordinator tempCoordinator;
        tempCoordinator.clearExclusions();
    }
    DBG("Plugin exclusion list cleared. Previously problematic plugins will be scanned again.");
}

PluginScanCoordinator* TracktionEngineWrapper::getPluginScanCoordinator() {
    if (!pluginScanCoordinator_)
        pluginScanCoordinator_ = std::make_unique<PluginScanCoordinator>();
    return pluginScanCoordinator_.get();
}

juce::KnownPluginList& TracktionEngineWrapper::getKnownPluginList() {
    return engine_->getPluginManager().knownPluginList;
}

const juce::KnownPluginList& TracktionEngineWrapper::getKnownPluginList() const {
    return engine_->getPluginManager().knownPluginList;
}

juce::File TracktionEngineWrapper::getPluginListFile() const {
    // Store plugin list in app data directory
    // macOS: ~/Library/Application Support/MAGDA/
    // Windows: %APPDATA%/MAGDA/
    // Linux: ~/.config/MAGDA/
    auto appDataDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("MAGDA");

    // Create directory if it doesn't exist
    if (!appDataDir.exists()) {
        appDataDir.createDirectory();
    }

    return appDataDir.getChildFile("PluginList.xml");
}

void TracktionEngineWrapper::savePluginList() {
    if (!engine_) {
        DBG("Cannot save plugin list: engine not initialized");
        return;
    }

    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    auto pluginListFile = getPluginListFile();

    // Create XML representation of the plugin list
    if (auto xml = knownPlugins.createXml()) {
        if (xml->writeTo(pluginListFile)) {
            DBG("Saved plugin list (" << knownPlugins.getNumTypes()
                                      << " plugins) to: " << pluginListFile.getFullPathName());
        } else {
            DBG("Failed to write plugin list to: " << pluginListFile.getFullPathName());
        }
    }
}

void TracktionEngineWrapper::loadPluginList() {
    if (!engine_) {
        DBG("Cannot load plugin list: engine not initialized");
        return;
    }

    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    auto pluginListFile = getPluginListFile();

    if (pluginListFile.existsAsFile()) {
        if (auto xml = juce::XmlDocument::parse(pluginListFile)) {
            knownPlugins.recreateFromXml(*xml);
            DBG("Loaded plugin list (" << knownPlugins.getNumTypes()
                                       << " plugins) from: " << pluginListFile.getFullPathName());
        } else {
            DBG("Failed to parse plugin list from: " << pluginListFile.getFullPathName());
            knownPlugins.clear();
        }
    } else {
        DBG("No saved plugin list found at: " << pluginListFile.getFullPathName());
        DBG("Plugins will need to be scanned manually via the Plugin Browser");
        knownPlugins.clear();
    }
}

void TracktionEngineWrapper::clearPluginList() {
    if (!engine_) {
        DBG("Cannot clear plugin list: engine not initialized");
        return;
    }

    // Clear in-memory list
    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    knownPlugins.clear();

    // Delete the saved file
    auto pluginListFile = getPluginListFile();
    if (pluginListFile.existsAsFile()) {
        pluginListFile.deleteFile();
        DBG("Deleted plugin list file: " << pluginListFile.getFullPathName());
    }

    DBG("Plugin list cleared. Use 'Scan' to rediscover plugins.");
}

void TracktionEngineWrapper::detectNewPlugins(
    std::function<void(const juce::String&)> statusCallback) {
    if (!engine_) {
        DBG("[AutoDetect] Engine not initialized");
        return;
    }

    juce::Logger::writeToLog("[AutoDetect] Checking for new plugins...");
    if (statusCallback)
        statusCallback("Checking for new plugins...");

    // Snapshot all data needed by the background thread while on the message thread
    auto& pluginManager = engine_->getPluginManager();
    auto& formatManager = pluginManager.pluginFormatManager;

    // Snapshot known plugin paths
    juce::StringArray knownPaths;
    auto& knownPlugins = pluginManager.knownPluginList;
    for (int i = 0; i < knownPlugins.getNumTypes(); ++i) {
        auto* desc = knownPlugins.getType(i);
        if (desc)
            knownPaths.add(desc->fileOrIdentifier);
    }

    if (!pluginScanCoordinator_)
        pluginScanCoordinator_ = std::make_unique<PluginScanCoordinator>();

    // Snapshot excluded paths and custom paths (avoids data race on background thread)
    juce::StringArray excludedPaths;
    for (const auto& entry : pluginScanCoordinator_->getExcludedPlugins())
        excludedPaths.add(entry.path);
    auto customPaths = Config::getInstance().getCustomPluginPaths();

    // Join any previous discovery thread before starting a new one
    if (pluginDiscoveryThread_.joinable())
        pluginDiscoveryThread_.join();

    pluginDiscoveryThread_ = std::thread([this, &formatManager, knownPaths, excludedPaths,
                                          customPaths, statusCallback]() {
        // Discover all plugin files on disk (expensive recursive dir traversal)
        // Using snapshots of excluded/custom paths to avoid data races
        auto allPlugins =
            PluginScanCoordinator::discoverPluginFiles(formatManager, excludedPaths, customPaths);

        // Find new plugins not in the known list
        std::vector<PluginScanCoordinator::PluginToScan> newPlugins;
        for (const auto& plugin : allPlugins) {
            if (!knownPaths.contains(plugin.pluginPath))
                newPlugins.push_back(plugin);
        }

        // Build disk paths set for stale detection
        juce::StringArray diskPaths;
        for (const auto& plugin : allPlugins)
            diskPaths.add(plugin.pluginPath);

        // Switch back to message thread for UI updates and scan dispatch
        auto alive = aliveFlag_;
        if (!*alive)
            return;

        juce::WeakReference<TracktionEngineWrapper> weakThis(this);
        juce::MessageManager::callAsync([weakThis, alive, newPlugins = std::move(newPlugins),
                                         diskPaths = std::move(diskPaths),
                                         statusCallback]() mutable {
            auto* self = weakThis.get();
            if (!self || !*alive || !self->engine_)
                return;

            auto& pm = self->engine_->getPluginManager();
            auto& kp = pm.knownPluginList;
            auto& fm = pm.pluginFormatManager;

            // Remove stale plugins
            juce::Array<juce::PluginDescription> stalePlugins;
            for (int i = 0; i < kp.getNumTypes(); ++i) {
                auto* desc = kp.getType(i);
                if (desc && !diskPaths.contains(desc->fileOrIdentifier)) {
                    juce::File pluginFile(desc->fileOrIdentifier);
                    if (pluginFile.getFullPathName().isNotEmpty() && !pluginFile.exists()) {
                        DBG("[AutoDetect] Removing stale plugin: "
                            << desc->name << " (" << desc->fileOrIdentifier << ")");
                        stalePlugins.add(*desc);
                    }
                }
            }
            for (const auto& desc : stalePlugins)
                kp.removeType(desc);

            if (newPlugins.empty() && stalePlugins.isEmpty()) {
                auto msg = "Plugins up to date (" + juce::String(kp.getNumTypes()) + " loaded)";
                juce::Logger::writeToLog("[AutoDetect] " + msg);
                if (statusCallback)
                    statusCallback(msg);
                return;
            }

            if (!stalePlugins.isEmpty()) {
                auto msg = "Removed " + juce::String(stalePlugins.size()) + " stale plugin(s)";
                juce::Logger::writeToLog("[AutoDetect] " + msg);
                if (statusCallback)
                    statusCallback(msg);
                self->savePluginList();
            }

            if (newPlugins.empty())
                return;

            auto msg = "Scanning " + juce::String(static_cast<int>(newPlugins.size())) +
                       " new plugin(s)...";
            juce::Logger::writeToLog("[AutoDetect] " + msg);
            if (statusCallback)
                statusCallback(msg);

            self->isScanning_ = true;

            self->pluginScanCoordinator_->startIncrementalScan(
                fm, newPlugins,
                [statusCallback](float, const juce::String& currentPlugin) {
                    if (statusCallback) {
                        auto name = juce::File(currentPlugin).getFileNameWithoutExtension();
                        statusCallback("Scanning: " + name + "...");
                    }
                },
                [weakThis, alive](bool /*success*/,
                                  const juce::Array<juce::PluginDescription>& plugins,
                                  const juce::StringArray& failedPlugins) {
                    auto* s = weakThis.get();
                    if (!s || !*alive || !s->engine_)
                        return;
                    auto& kpl = s->engine_->getPluginManager().knownPluginList;
                    for (const auto& desc : plugins)
                        kpl.addType(desc);

                    auto msg =
                        "[AutoDetect] Incremental scan complete: " + juce::String(plugins.size()) +
                        " new plugin(s) added" +
                        (failedPlugins.size() > 0
                             ? ", " + juce::String(failedPlugins.size()) + " failed"
                             : "");
                    DBG(msg);
                    juce::Logger::writeToLog(msg);
                    s->savePluginList();
                    s->isScanning_ = false;
                });
        });
    });
}

}  // namespace magda
