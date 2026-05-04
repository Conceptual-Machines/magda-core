#include <map>
#include <set>
#include <thread>
#include <utility>

#include "../core/AppPaths.hpp"
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

    // "Scan All Plugins" means really all of them — bust the exclusion
    // cache so previously-failing plugins get another chance. A plugin
    // that genuinely still crashes will be re-added to the list during
    // this scan; a plugin that the user has fixed (un-quarantined,
    // updated, installed a missing dependency) will succeed and stay.
    // Without this step a plugin that ever fails once is excluded
    // forever, even after the underlying problem is gone (#1005).
    pluginScanCoordinator_->clearExclusions();

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
        [this, &knownPlugins, &formatManager](bool success,
                                              const juce::Array<juce::PluginDescription>& plugins,
                                              const juce::StringArray& failedPlugins) {
            // Drop entries whose (path, format) was rescanned but whose uid
            // is not in the fresh results — the vendor bumped the VST3
            // uniqueId across a version update and the old row would
            // otherwise survive forever as a phantom duplicate (#1005).
            removeSupersededEntries(knownPlugins, plugins);

            // Add found plugins to KnownPluginList
            for (const auto& desc : plugins) {
                knownPlugins.addType(desc);
            }

            // Remove entries whose plugins are no longer installed (e.g. the
            // plugin was uninstalled between the last scan and this one).
            // The unconditional savePluginList() + Config update below covers
            // persistence for both the additions and the pruning, so we
            // don't pay for an extra save here.
            pruneMissingPlugins(knownPlugins, formatManager);

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
    // Routed via paths::pluginListFile() — respects MAGDA_DATA_DIR /
    // Config::getDataDir() override. Defaults to userApplicationDataDirectory.
    auto file = magda::paths::pluginListFile();
    file.getParentDirectory().createDirectory();
    return file;
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

int TracktionEngineWrapper::removeSupersededEntries(
    juce::KnownPluginList& knownPlugins, const juce::Array<juce::PluginDescription>& freshScan) {
    using Key = std::pair<juce::String, juce::String>;
    using UidPair = std::pair<int, int>;
    std::map<Key, std::set<UidPair>> validUids;
    for (const auto& desc : freshScan) {
        validUids[{desc.fileOrIdentifier, desc.pluginFormatName}].insert(
            {desc.deprecatedUid, desc.uniqueId});
    }

    juce::Array<juce::PluginDescription> superseded;
    for (int i = 0; i < knownPlugins.getNumTypes(); ++i) {
        auto* desc = knownPlugins.getType(i);
        if (!desc)
            continue;
        const Key key{desc->fileOrIdentifier, desc->pluginFormatName};
        auto it = validUids.find(key);
        if (it == validUids.end())
            continue;  // path/format wasn't scanned this run; leave it alone
        if (it->second.count({desc->deprecatedUid, desc->uniqueId}) == 0)
            superseded.add(*desc);
    }

    for (const auto& desc : superseded) {
        DBG("Removing superseded plugin entry: " << desc.name << " uid=0x"
                                                 << juce::String::toHexString(desc.uniqueId) << " ("
                                                 << desc.fileOrIdentifier << ")");
        knownPlugins.removeType(desc);
    }

    if (!superseded.isEmpty())
        DBG("Removed " << superseded.size() << " superseded plugin entry/entries");

    return superseded.size();
}

int TracktionEngineWrapper::pruneMissingPlugins(juce::KnownPluginList& knownPlugins,
                                                juce::AudioPluginFormatManager& formatManager) {
    juce::Array<juce::PluginDescription> stalePlugins;
    for (int i = 0; i < knownPlugins.getNumTypes(); ++i) {
        auto* desc = knownPlugins.getType(i);
        if (!desc)
            continue;
        if (!formatManager.doesPluginStillExist(*desc))
            stalePlugins.add(*desc);
    }

    for (const auto& desc : stalePlugins) {
        DBG("Removing stale plugin: " << desc.name << " (" << desc.fileOrIdentifier << ")");
        knownPlugins.removeType(desc);
    }

    if (!stalePlugins.isEmpty())
        DBG("Pruned " << stalePlugins.size() << " stale plugin(s) from known list");

    return stalePlugins.size();
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
    std::function<void(IncrementalScanPhase, const juce::String&)> statusCallback,
    std::function<void(bool, int, int, const juce::StringArray&)> completionCallback) {
    if (!engine_) {
        DBG("[AutoDetect] Engine not initialized");
        return;
    }

    juce::Logger::writeToLog("[AutoDetect] Checking for new plugins...");
    if (statusCallback)
        statusCallback(IncrementalScanPhase::Discovering, {});

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
                                          customPaths, statusCallback, completionCallback]() {
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

        // Switch back to message thread for UI updates and scan dispatch
        auto alive = aliveFlag_;
        if (!*alive)
            return;

        juce::WeakReference<TracktionEngineWrapper> weakThis(this);
        juce::MessageManager::callAsync([weakThis, alive, newPlugins = std::move(newPlugins),
                                         statusCallback, completionCallback]() mutable {
            auto* self = weakThis.get();
            if (!self || !*alive || !self->engine_)
                return;

            auto& pm = self->engine_->getPluginManager();
            auto& kp = pm.knownPluginList;
            auto& fm = pm.pluginFormatManager;

            if (newPlugins.empty()) {
                juce::Logger::writeToLog("[AutoDetect] Plugins up to date (" +
                                         juce::String(kp.getNumTypes()) + " loaded)");
                if (statusCallback)
                    statusCallback(IncrementalScanPhase::UpToDate, {});
                if (completionCallback)
                    completionCallback(true, 0, kp.getNumTypes(), {});
                return;
            }

            juce::Logger::writeToLog("[AutoDetect] Scanning " +
                                     juce::String(static_cast<int>(newPlugins.size())) +
                                     " new plugin(s)...");

            self->isScanning_ = true;

            self->pluginScanCoordinator_->startIncrementalScan(
                fm, newPlugins,
                [statusCallback](float, const juce::String& currentPlugin) {
                    if (statusCallback)
                        statusCallback(IncrementalScanPhase::Scanning, currentPlugin);
                },
                [weakThis, alive, completionCallback](
                    bool success, const juce::Array<juce::PluginDescription>& plugins,
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
                    if (completionCallback)
                        completionCallback(success, plugins.size(), kpl.getNumTypes(),
                                           failedPlugins);
                });
        });
    });
}

}  // namespace magda
