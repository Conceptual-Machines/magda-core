#include "TracktionEngineWrapper.hpp"

#include <iostream>

#include "PluginScanCoordinator.hpp"

namespace magda {

// =============================================================================
// Plugin Scanning
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
    std::cout << "Starting plugin scan with OUT-OF-PROCESS scanner" << std::endl;
    std::cout << "Available formats: " << formatNames.joinIntoString(", ") << std::endl;

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

            int numPlugins = knownPlugins.getNumTypes();
            std::cout << "Plugin scan complete. Found " << numPlugins << " plugins." << std::endl;

            if (failedPlugins.size() > 0) {
                std::cout << "Failed/crashed plugins (" << failedPlugins.size()
                          << "):" << std::endl;
                for (const auto& failed : failedPlugins) {
                    std::cout << "  - " << failed << std::endl;
                }
            }

            // Save the updated plugin list to persistent storage
            savePluginList();

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

void TracktionEngineWrapper::clearPluginScanCrashFiles() {
    // Clear the blacklist in the coordinator
    if (pluginScanCoordinator_) {
        pluginScanCoordinator_->clearBlacklist();
    } else {
        // Create a temporary coordinator just to clear the blacklist
        PluginScanCoordinator tempCoordinator;
        tempCoordinator.clearBlacklist();
    }
    std::cout << "Plugin blacklist cleared. Previously problematic plugins will be scanned again."
              << std::endl;
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
        std::cerr << "Cannot save plugin list: engine not initialized" << std::endl;
        return;
    }

    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    auto pluginListFile = getPluginListFile();

    // Create XML representation of the plugin list
    if (auto xml = knownPlugins.createXml()) {
        if (xml->writeTo(pluginListFile)) {
            std::cout << "Saved plugin list (" << knownPlugins.getNumTypes()
                      << " plugins) to: " << pluginListFile.getFullPathName() << std::endl;
        } else {
            std::cerr << "Failed to write plugin list to: " << pluginListFile.getFullPathName()
                      << std::endl;
        }
    }
}

void TracktionEngineWrapper::loadPluginList() {
    if (!engine_) {
        std::cerr << "Cannot load plugin list: engine not initialized" << std::endl;
        return;
    }

    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    auto pluginListFile = getPluginListFile();

    if (pluginListFile.existsAsFile()) {
        if (auto xml = juce::XmlDocument::parse(pluginListFile)) {
            knownPlugins.recreateFromXml(*xml);
            std::cout << "Loaded plugin list (" << knownPlugins.getNumTypes()
                      << " plugins) from: " << pluginListFile.getFullPathName() << std::endl;
        } else {
            std::cerr << "Failed to parse plugin list from: " << pluginListFile.getFullPathName()
                      << std::endl;
            knownPlugins.clear();
        }
    } else {
        std::cout << "No saved plugin list found at: " << pluginListFile.getFullPathName()
                  << std::endl;
        std::cout << "Plugins will need to be scanned manually via the Plugin Browser" << std::endl;
        knownPlugins.clear();
    }
}

void TracktionEngineWrapper::clearPluginList() {
    if (!engine_) {
        std::cerr << "Cannot clear plugin list: engine not initialized" << std::endl;
        return;
    }

    // Clear in-memory list
    auto& knownPlugins = engine_->getPluginManager().knownPluginList;
    knownPlugins.clear();

    // Delete the saved file
    auto pluginListFile = getPluginListFile();
    if (pluginListFile.existsAsFile()) {
        pluginListFile.deleteFile();
        std::cout << "Deleted plugin list file: " << pluginListFile.getFullPathName() << std::endl;
    }

    std::cout << "Plugin list cleared. Use 'Scan' to rediscover plugins." << std::endl;
}

}  // namespace magda
