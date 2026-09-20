#include "PluginService.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <utility>

#if defined(__APPLE__)
    #include <sys/mount.h>
#endif

#include "../audio/plugin_manager/ExternalPluginState.hpp"
#include "../core/AppPaths.hpp"
#include "DeviceParameterScan.hpp"
#include "PluginMetadataStore.hpp"
#include "PluginScanCoordinator.hpp"
#include "core/Config.hpp"
#include "core/PluginPreferences.hpp"

namespace magda {
namespace {

std::optional<juce::String> externalVolumeRootForPath(const juce::String& path) {
#if JUCE_MAC
    const juce::String prefix = "/Volumes/";
    if (!path.startsWith(prefix))
        return std::nullopt;

    const int volumeEnd = path.indexOfChar(prefix.length(), '/');
    if (volumeEnd < 0)
        return std::nullopt;
    return path.substring(0, volumeEnd);
#elif JUCE_WINDOWS
    if (path.length() >= 3 && juce::CharacterFunctions::isLetter(path[0]) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return path.substring(0, 3);
    }

    auto normalized = path.replaceCharacter('\\', '/');
    if (!normalized.startsWith("//"))
        return std::nullopt;
    const int serverEnd = normalized.indexOfChar(2, '/');
    if (serverEnd < 0)
        return std::nullopt;
    const int shareEnd = normalized.indexOfChar(serverEnd + 1, '/');
    if (shareEnd < 0)
        return std::nullopt;
    return normalized.substring(0, shareEnd);
#elif JUCE_LINUX
    auto rootWithComponents = [&path](const juce::String& prefix,
                                      int componentCount) -> std::optional<juce::String> {
        if (!path.startsWith(prefix))
            return std::nullopt;
        int end = static_cast<int>(prefix.length());
        for (int component = 0; component < componentCount; ++component) {
            end = path.indexOfChar(end, '/');
            if (end < 0)
                return std::nullopt;
            if (component + 1 < componentCount)
                ++end;
        }
        return path.substring(0, end);
    };

    if (auto root = rootWithComponents("/run/media/", 2))
        return root;
    if (auto root = rootWithComponents("/media/", 2))
        return root;
    return rootWithComponents("/mnt/", 1);
#else
    juce::ignoreUnused(path);
    return std::nullopt;
#endif
}

#if JUCE_LINUX
juce::String decodeMountInfoPath(const juce::String& encoded) {
    return encoded.replace("\\040", " ")
        .replace("\\011", "\t")
        .replace("\\012", "\n")
        .replace("\\134", "\\");
}
#endif

bool isVolumeMounted(const juce::String& root) {
#if JUCE_MAC
    struct statfs* mounts = nullptr;
    const int count = getmntinfo(&mounts, MNT_NOWAIT);
    for (int i = 0; i < count; ++i) {
        if (juce::String::fromUTF8(mounts[i].f_mntonname) == root)
            return true;
    }
    return false;
#elif JUCE_LINUX
    std::ifstream input("/proc/self/mountinfo");
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string mountId;
        std::string parentId;
        std::string device;
        std::string filesystemRoot;
        std::string mountPoint;
        if (fields >> mountId >> parentId >> device >> filesystemRoot >> mountPoint) {
            if (decodeMountInfoPath(juce::String::fromUTF8(mountPoint.c_str())) == root)
                return true;
        }
    }
    return false;
#elif JUCE_WINDOWS
    return juce::File(root).isDirectory();
#else
    juce::ignoreUnused(root);
    return true;
#endif
}

bool shouldPreserveUnavailablePlugin(
    const juce::PluginDescription& desc,
    const std::function<bool(const juce::String&)>& volumeIsMounted) {
    if (!juce::File::isAbsolutePath(desc.fileOrIdentifier))
        return false;
    const auto root = externalVolumeRootForPath(desc.fileOrIdentifier);
    return root.has_value() && !volumeIsMounted(*root);
}

}  // namespace

PluginService& PluginService::getInstance() {
    static PluginService service;
    return service;
}

PluginService::PluginService() = default;

PluginService::~PluginService() {
    *alive_ = false;
    if (discoveryThread_.joinable())
        discoveryThread_.join();
}

void PluginService::useEngineList(juce::AudioPluginFormatManager& formats,
                                  juce::KnownPluginList& list) {
    ++attachment_;
    formats_ = &formats;
    list_ = &list;
}

void PluginService::forgetEngineList() {
    // Both write through the borrowed pair on a later turn: the coordinator's completion
    // callback off its timer, the discovery thread off its message-thread hop. The
    // coordinator outlives the engine now that the service owns it, so a scan left running
    // here would finish against a destroyed list.
    abortScan();
    if (discoveryThread_.joinable())
        discoveryThread_.join();

    ++attachment_;
    formats_ = nullptr;
    list_ = nullptr;
    internalScanner_ = nullptr;
}

PluginScanCoordinator& PluginService::coordinator() const {
    if (!coordinator_)
        coordinator_ = std::make_unique<PluginScanCoordinator>();
    return *coordinator_;
}

// --- the list -----------------------------------------------------------------

juce::File PluginService::listFile() {
    // Routed via paths::pluginMetadataFile() — respects MAGDA_DATA_DIR /
    // Config::getDataDir() override. Defaults to userApplicationDataDirectory.
    auto file = magda::paths::pluginMetadataFile();
    file.getParentDirectory().createDirectory();
    return file;
}

void PluginService::loadList() {
    if (!list_)
        return;

    metadataLoaded_ = false;
    try {
        auto& store = PluginMetadataStore::defaultForCurrentThread();
        store.loadKnownPlugins(*list_);
        metadataLoaded_ = true;
        DBG("Loaded plugin metadata (" << list_->getNumTypes()
                                       << " plugins) from: " << store.file().getFullPathName());
    } catch (const std::exception& e) {
        DBG("Failed to load plugin metadata: " << e.what());
    }
}

void PluginService::saveList() {
    if (!list_)
        return;
    if (!metadataLoaded_) {
        DBG("Skipping plugin metadata save because the last load failed");
        return;
    }

    try {
        auto& store = PluginMetadataStore::defaultForCurrentThread();
        store.saveKnownPlugins(*list_);
        DBG("Saved plugin metadata (" << list_->getNumTypes()
                                      << " plugins) to: " << store.file().getFullPathName());
    } catch (const std::exception& e) {
        DBG("Failed to save plugin metadata: " << e.what());
    }
}

void PluginService::clearList() {
    if (!list_)
        return;

    list_->clear();
    try {
        auto& store = PluginMetadataStore::defaultForCurrentThread();
        store.clearKnownPlugins();
        metadataLoaded_ = true;
        DBG("Cleared scanned plugins from: " << store.file().getFullPathName());
    } catch (const std::exception& e) {
        DBG("Failed to clear plugin metadata: " << e.what());
    }
}

void PluginService::openList(bool autoDetectNewPlugins) {
    if (!list_ || !formats_)
        return;

    loadList();

    // Unconditional: the scan-on-startup setting only governs detecting *new* plugins.
    // The count is resynced so the settings dialog doesn't show a stale total.
    if (pruneMissingPlugins(*list_, *formats_) > 0) {
        saveList();
        Config::getInstance().setTotalPluginCount(list_->getNumTypes());
        Config::getInstance().save();
    }

    if (!autoDetectNewPlugins)
        return;

    // The splash wants a flat string, so the phase is formatted here.
    auto status = onScanStatus_;
    detectNewPlugins(
        [status](PluginScanPhase phase, const juce::String& currentPlugin) {
            if (!status)
                return;
            switch (phase) {
                case PluginScanPhase::Discovering:
                    status("Checking for new plugins...");
                    break;
                case PluginScanPhase::UpToDate:
                    status("Plugins up to date");
                    break;
                case PluginScanPhase::Scanning:
                    status("Scanning: " + pluginDisplayName(currentPlugin));
                    break;
            }
        },
        nullptr);
}

juce::Array<juce::PluginDescription> PluginService::knownTypes() const {
    return list_ ? list_->getTypes() : juce::Array<juce::PluginDescription>{};
}

juce::Array<juce::PluginDescription> PluginService::preferredTypes() const {
    return PluginPreferences::getInstance().preferredExternalPlugins(knownTypes());
}

void PluginService::addListChangeListener(juce::ChangeListener* listener) {
    if (list_)
        list_->addChangeListener(listener);
}

void PluginService::removeListChangeListener(juce::ChangeListener* listener) {
    if (list_)
        list_->removeChangeListener(listener);
}

int PluginService::removeSupersededEntries(juce::KnownPluginList& knownPlugins,
                                           const juce::Array<juce::PluginDescription>& freshScan) {
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

int PluginService::pruneMissingPlugins(juce::KnownPluginList& knownPlugins,
                                       juce::AudioPluginFormatManager& formatManager,
                                       std::function<bool(const juce::String&)> volumeIsMounted) {
    if (!volumeIsMounted)
        volumeIsMounted = isVolumeMounted;

    juce::Array<juce::PluginDescription> stalePlugins;
    for (int i = 0; i < knownPlugins.getNumTypes(); ++i) {
        auto* desc = knownPlugins.getType(i);
        if (!desc)
            continue;
        if (formatManager.doesPluginStillExist(*desc))
            continue;
        if (!shouldPreserveUnavailablePlugin(*desc, volumeIsMounted)) {
            stalePlugins.add(*desc);
        } else {
            DBG("Keeping unavailable plugin on unmounted volume: "
                << desc->name << " (" << desc->fileOrIdentifier << ")");
        }
    }

    for (const auto& desc : stalePlugins) {
        DBG("Removing stale plugin: " << desc.name << " (" << desc.fileOrIdentifier << ")");
        knownPlugins.removeType(desc);
    }

    if (!stalePlugins.isEmpty())
        DBG("Pruned " << stalePlugins.size() << " stale plugin(s) from known list");

    return stalePlugins.size();
}

// --- the scan -----------------------------------------------------------------

void PluginService::startScan(std::function<void(float, const juce::String&)> progressCallback) {
    if (!list_ || !formats_ || scanning_)
        return;

    scanning_ = true;

    juce::StringArray formatNames;
    for (int i = 0; i < formats_->getNumFormats(); ++i) {
        if (auto* format = formats_->getFormat(i))
            formatNames.add(format->getName());
    }
    DBG("Starting plugin scan with OUT-OF-PROCESS scanner");
    DBG("Available formats: " << formatNames.joinIntoString(", "));

    // "Scan All Plugins" means really all of them, so a plugin that ever failed once is
    // not excluded forever after the underlying problem is gone (#1005).
    coordinator().clearExclusions();

    coordinator().startScan(*formats_, progressCallback,
                            [this](bool success,
                                   const juce::Array<juce::PluginDescription>& plugins,
                                   const juce::StringArray& failedPlugins) {
                                // A vendor that bumped a VST3 uniqueId across a version update
                                // would otherwise leave the old row behind as a phantom duplicate
                                // (#1005).
                                removeSupersededEntries(*list_, plugins);

                                for (const auto& desc : plugins)
                                    list_->addType(desc);

                                // The save below covers both the additions and the pruning.
                                pruneMissingPlugins(*list_, *formats_);

                                const int numPlugins = list_->getNumTypes();
                                DBG("Plugin scan complete. Found " << numPlugins << " plugins.");

                                if (failedPlugins.size() > 0) {
                                    DBG("Failed/crashed plugins (" << failedPlugins.size() << "):");
                                    for (const auto& failed : failedPlugins)
                                        DBG("  - " << failed);
                                }

                                saveList();

                                if (success) {
                                    Config::getInstance().setTotalPluginCount(numPlugins);
                                    Config::getInstance().save();
                                }

                                scanning_ = false;

                                if (onScanComplete_)
                                    onScanComplete_(success, numPlugins, failedPlugins);
                            });
}

void PluginService::abortScan() {
    if (coordinator_)
        coordinator_->abortScan();
    scanning_ = false;
}

void PluginService::detectNewPlugins(
    std::function<void(PluginScanPhase, const juce::String&)> statusCallback,
    std::function<void(bool, int, int, const juce::StringArray&)> completionCallback) {
    if (!list_ || !formats_)
        return;

    juce::Logger::writeToLog("[AutoDetect] Checking for new plugins...");
    if (statusCallback)
        statusCallback(PluginScanPhase::Discovering, {});

    // Snapshot everything the background thread reads while still on the message thread.
    juce::StringArray knownPaths;
    for (int i = 0; i < list_->getNumTypes(); ++i) {
        if (auto* desc = list_->getType(i))
            knownPaths.add(desc->fileOrIdentifier);
    }

    juce::StringArray excludedPaths;
    for (const auto& entry : coordinator().getExcludedPlugins())
        excludedPaths.add(entry.path);
    auto customPaths = Config::getInstance().getCustomPluginPaths();

    if (discoveryThread_.joinable())
        discoveryThread_.join();

    auto& formats = *formats_;
    // Joining the thread below only guarantees its callAsync was queued, not that it ran.
    // A shutdown and re-initialise inside one drain would otherwise let this engine's
    // results scan into the next engine's list (#2756).
    const auto attachment = attachment_;
    discoveryThread_ = std::thread([this, &formats, attachment, knownPaths, excludedPaths,
                                    customPaths, statusCallback, completionCallback]() {
        // Expensive recursive directory traversal, off the snapshots above.
        auto allPlugins =
            PluginScanCoordinator::discoverPluginFiles(formats, excludedPaths, customPaths);

        std::vector<PluginScanCoordinator::PluginToScan> newPlugins;
        for (const auto& plugin : allPlugins) {
            if (!knownPaths.contains(plugin.pluginPath))
                newPlugins.push_back(plugin);
        }

        auto alive = alive_;
        if (!*alive)
            return;

        juce::MessageManager::callAsync([this, alive, attachment,
                                         newPlugins = std::move(newPlugins), statusCallback,
                                         completionCallback]() mutable {
            if (!*alive || attachment != attachment_)
                return;

            if (newPlugins.empty()) {
                juce::Logger::writeToLog("[AutoDetect] Plugins up to date (" +
                                         juce::String(list_->getNumTypes()) + " loaded)");
                if (statusCallback)
                    statusCallback(PluginScanPhase::UpToDate, {});
                if (completionCallback)
                    completionCallback(true, 0, list_->getNumTypes(), {});
                return;
            }

            juce::Logger::writeToLog("[AutoDetect] Scanning " +
                                     juce::String(static_cast<int>(newPlugins.size())) +
                                     " new plugin(s)...");

            scanning_ = true;

            coordinator().startIncrementalScan(
                *formats_, newPlugins,
                [statusCallback](float, const juce::String& currentPlugin) {
                    if (statusCallback)
                        statusCallback(PluginScanPhase::Scanning, currentPlugin);
                },
                [this, alive, attachment, completionCallback](
                    bool success, const juce::Array<juce::PluginDescription>& plugins,
                    const juce::StringArray& failedPlugins) {
                    if (!*alive || attachment != attachment_)
                        return;

                    for (const auto& desc : plugins)
                        list_->addType(desc);

                    auto msg =
                        "[AutoDetect] Incremental scan complete: " + juce::String(plugins.size()) +
                        " new plugin(s) added" +
                        (failedPlugins.size() > 0
                             ? ", " + juce::String(failedPlugins.size()) + " failed"
                             : "");
                    DBG(msg);
                    juce::Logger::writeToLog(msg);
                    saveList();
                    scanning_ = false;
                    if (completionCallback)
                        completionCallback(success, plugins.size(), list_->getNumTypes(),
                                           failedPlugins);
                });
        });
    });
}

bool PluginService::isScanRunning() const {
    return scanning_ || (coordinator_ && coordinator_->isScanning());
}

std::vector<ExcludedPlugin> PluginService::excludedPlugins() const {
    return coordinator().getExcludedPlugins();
}

void PluginService::setExcludedPlugins(const std::vector<ExcludedPlugin>& excluded) {
    auto& c = coordinator();
    c.clearExclusions();
    for (const auto& entry : excluded)
        c.excludePlugin(entry.path, entry.reason);
}

void PluginService::clearExclusions() {
    coordinator().clearExclusions();
}

juce::File PluginService::scanReportFile() const {
    return PluginScanCoordinator::getScanReportFile();
}

std::vector<std::string> PluginService::systemSearchPaths() const {
    std::vector<std::string> paths;
    if (!formats_)
        return paths;

    for (int i = 0; i < formats_->getNumFormats(); ++i) {
        auto* format = formats_->getFormat(i);
        if (format == nullptr)
            continue;
        const auto formatName = format->getName();
        if (!formatName.containsIgnoreCase("VST3") && !formatName.containsIgnoreCase("AudioUnit"))
            continue;
        const auto searchPaths = format->getDefaultLocationsToSearch();
        for (int j = 0; j < searchPaths.getNumPaths(); ++j) {
            auto path = searchPaths[j].getFullPathName().toStdString();
            if (!std::ranges::contains(paths, path))
                paths.push_back(std::move(path));
        }
    }
    return paths;
}

// --- parameters ---------------------------------------------------------------

std::vector<ScannedPluginParameter> PluginService::scanParameters(const juce::String& pluginId,
                                                                  bool internalPlugin) {
    std::vector<ScannedPluginParameter> result;

    // A MAGDA device answers for itself, off the catalog rather than out of an Edit. Its own
    // metadata is what the host wraps anyway, and the native engine builds no Edit to put a
    // plugin in (#2601).
    if (internalPlugin) {
        if (auto scanned = scanDeviceParameters(pluginId); !scanned.empty())
            return scanned;
        return internalScanner_ ? internalScanner_(pluginId) : result;
    }

    if (!formats_)
        return result;

    constexpr std::array<float, 5> samplePoints{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    const auto identifierOf = [](const juce::PluginDescription& candidate) {
        return candidate.createIdentifierString();
    };
    const auto types = knownTypes();
    const auto match = std::ranges::find(types, pluginId, identifierOf);
    if (match == types.end())
        return result;
    const juce::PluginDescription description = *match;

    juce::String error;
    auto instance = formats_->createPluginInstance(description, 44100.0, 512, error);
    if (instance == nullptr)
        return result;

    // The host's own list rather than the plugin's raw array, because that is what
    // DeviceInfo::parameters is built from: same filter, same order, same names. A config
    // entry is stored by its position in the list, so a scan that filtered differently --
    // this one dropped unnamed parameters, which the model keeps -- saved every later
    // override onto the wrong control (#2601). The wrapper pair is the host's own and is not
    // configurable, so it is not offered.
    const auto described = magda::describeHostParameters(*instance, magda::DeviceInfo{});
    const auto order = magda::hostParameterOrder(*instance);

    for (const auto& model : described.parameters) {
        // The host slot addresses the live parameter; the position in this list is what a
        // scan result is addressed by. The two differ by the wrapper pair, and everything
        // downstream -- the dialog's rows, the config file's entries -- counts positions.
        auto* parameter = order[static_cast<std::size_t>(model.paramIndex)];

        ScannedPluginParameter info;
        info.name = model.name;
        info.stableId = model.stableId;
        info.defaultValue = parameter->getDefaultValue();
        const auto rawLabel = parameter->getLabel().trim();
        info.unit = rawLabel.length() <= 6 && !rawLabel.contains("[") && !rawLabel.contains("(")
                        ? (rawLabel.isEmpty() ? juce::String("%") : rawLabel)
                        : juce::String("%");
        info.scanInput.paramIndex = static_cast<int>(result.size());
        info.scanInput.name = info.name;
        info.scanInput.label = rawLabel;

        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter)) {
            const auto range = ranged->getNormalisableRange();
            info.rangeMin = range.start;
            info.rangeMax = range.end;
        }
        info.rangeCenter = (info.rangeMin + info.rangeMax) * 0.5f;
        info.scanInput.rangeMin = info.rangeMin;
        info.scanInput.rangeMax = info.rangeMax;
        info.scanInput.stateCount = parameter->getNumSteps();
        if (info.scanInput.stateCount > 1000)
            info.scanInput.stateCount = 0;

        if (info.scanInput.stateCount > 0) {
            for (int state = 0; state < info.scanInput.stateCount; ++state) {
                const float normalized =
                    info.scanInput.stateCount == 1
                        ? 0.0f
                        : static_cast<float>(state) /
                              static_cast<float>(info.scanInput.stateCount - 1);
                info.scanInput.displayTexts.push_back(parameter->getText(normalized, 128));
            }
            info.valueTable = info.scanInput.displayTexts;
        } else {
            for (const auto sample : samplePoints)
                info.scanInput.displayTexts.push_back(parameter->getText(sample, 128));

            const auto startsWithALetter = [](const juce::String& text) {
                const auto trimmed = text.trim();
                return trimmed.isNotEmpty() && juce::CharacterFunctions::isLetter(trimmed[0]);
            };
            if (std::ranges::all_of(info.scanInput.displayTexts, startsWithALetter)) {
                info.scanInput.displayTexts.clear();
                for (int state = 0; state <= 1000; ++state) {
                    const auto text = parameter->getText(static_cast<float>(state) / 1000.0f, 128);
                    if (info.scanInput.displayTexts.empty() ||
                        info.scanInput.displayTexts.back() != text)
                        info.scanInput.displayTexts.push_back(text);
                }
                info.valueTable = info.scanInput.displayTexts;
            } else {
                info.valueTable.reserve(1001);
                for (int state = 0; state <= 1000; ++state)
                    info.valueTable.push_back(
                        parameter->getText(static_cast<float>(state) / 1000.0f, 128));
            }
        }
        result.push_back(std::move(info));
    }
    return result;
}

}  // namespace magda
