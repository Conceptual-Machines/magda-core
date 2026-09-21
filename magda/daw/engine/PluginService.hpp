#pragma once

/**
 * @file PluginService.hpp
 * @brief The owner of plugin discovery and live hosted-plugin state (#2756, #2758).
 */

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../core/ParameterDetector.hpp"
#include "PluginExclusions.hpp"

namespace magda {

class PluginScanCoordinator;
struct ChainNodePath;

/** @brief The live plugin instances a PluginService reads and writes (#2758). */
class PluginStateProvider {
  public:
    virtual ~PluginStateProvider();

    virtual void captureAllPluginStates() = 0;
    virtual void capturePluginStateAt(const ChainNodePath& devicePath) = 0;
    virtual void applyPluginStateAt(const ChainNodePath& devicePath) = 0;

    /// Push an internal device's authored state from the model onto its instance (#2760).
    virtual void projectAuthoredStateAt(const ChainNodePath& devicePath) = 0;
};

enum class PluginScanPhase {
    Discovering,
    UpToDate,
    Scanning,
};

struct ScannedPluginParameter {
    juce::String name;

    /// The parameter's own id, which is what a saved config is matched by
    /// (PluginParameterConfigEntry::id). Empty for a parameter that declares
    /// none, which falls back to its position.
    juce::String stableId;

    float defaultValue = 0.5f;
    juce::String unit;
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
    float rangeCenter = 0.5f;
    ParameterScale scale = ParameterScale::Linear;
    std::vector<juce::String> valueTable;
    ParameterScanInput scanInput;
};

/**
 * @brief What plugins exist and the seam to the instances currently rendering. Message thread.
 *
 * A scan is not an engine question, and neither is the project operation of capturing a
 * hosted plugin's state, so neither lives on AudioEngine (#2756, #2758). Tracktion's Engine
 * still owns the KnownPluginList its own hosting reads, so until the fork goes (#2557) the
 * service is pointed at that pair rather than owning one.
 */
class PluginService {
  public:
    static PluginService& getInstance();

    PluginService(const PluginService&) = delete;
    PluginService& operator=(const PluginService&) = delete;

    /** @brief Answer off @p formats and @p list, which outlive this or are dropped first. */
    void useEngineList(juce::AudioPluginFormatManager& formats, juce::KnownPluginList& list);

    /// The engine is going away: end any scan, then forget its pair and its scanner.
    void forgetEngineList();

    /**
     * @brief Load the saved list, drop what is gone, and look for what is new.
     *
     * @p autoDetectNewPlugins is the user's setting; a headless run passes false whatever
     * it says, because the detect reports to a splash that is not there.
     */
    void openList(bool autoDetectNewPlugins);

    juce::AudioPluginFormatManager* formats() const {
        return formats_;
    }
    juce::KnownPluginList* knownList() const {
        return list_;
    }

    juce::Array<juce::PluginDescription> knownTypes() const;

    /**
     * @brief Plugins for browser and menu presentation, honouring the user's format
     * preference on macOS. knownList() is the exact-lookup half of the same question.
     */
    juce::Array<juce::PluginDescription> preferredTypes() const;

    void addListChangeListener(juce::ChangeListener* listener);
    void removeListChangeListener(juce::ChangeListener* listener);

    /**
     * @brief Rescan every plugin on the system, out of process.
     *
     * The exclusion cache is busted first, so a plugin the user has since fixed is not
     * excluded forever for having failed once (#1005).
     */
    void startScan(std::function<void(float, const juce::String&)> progressCallback);
    void abortScan();

    /**
     * @brief Scan only what is installed but not yet known.
     *
     * @p statusCallback receives a phase and the path being scanned (empty outside
     * Scanning), so callers format their own localized text. @p completionCallback's
     * addedCount is what this run added, totalCount the list size afterwards.
     */
    void detectNewPlugins(
        std::function<void(PluginScanPhase, const juce::String&)> statusCallback,
        std::function<void(bool, int, int, const juce::StringArray&)> completionCallback);

    void setScanCompletionCallback(std::function<void(bool, int, const juce::StringArray&)> cb) {
        onScanComplete_ = std::move(cb);
    }

    /// The splash screen's line, already formatted; openList() is what writes to it.
    void setScanStatusCallback(std::function<void(const juce::String&)> cb) {
        onScanStatus_ = std::move(cb);
    }

    bool isScanRunning() const;

    std::vector<ExcludedPlugin> excludedPlugins() const;
    void setExcludedPlugins(const std::vector<ExcludedPlugin>& excluded);
    void clearExclusions();
    juce::File scanReportFile() const;

    /** @brief Where VST3 and AudioUnit look by default, deduplicated. */
    std::vector<std::string> systemSearchPaths() const;

    /**
     * @brief Every parameter @p pluginId declares, for the parameter-config dialog.
     *
     * A MAGDA device answers off the catalog, an external plugin is opened straight from
     * the format manager, and a plugin that is neither is the fork's to answer (#2601).
     */
    std::vector<ScannedPluginParameter> scanParameters(const juce::String& pluginId,
                                                       bool internalPlugin);

    /// Tracktion's own internal plugins need an Edit to be built in, which only the fork has.
    void setInternalParameterScanner(
        std::function<std::vector<ScannedPluginParameter>(const juce::String&)> scanner) {
        internalScanner_ = std::move(scanner);
    }

    void saveList();
    void clearList();

    /**
     * @brief Make @p provider the source of live hosted-plugin state.
     * @return The provider this replaced, for a scoped test to restore explicitly.
     */
    PluginStateProvider* useStateProvider(PluginStateProvider& provider);

    /// Drop @p provider if it is still current; a replacement is never revived implicitly.
    void forgetStateProvider(const PluginStateProvider& provider);

    /** @brief Read every live hosted plugin's state back into the project model. */
    void captureAllPluginStates();

    /** @brief Read the hosted plugin at @p devicePath back into the project model. */
    void capturePluginStateAt(const ChainNodePath& devicePath);

    /** @brief Apply the project model's state at @p devicePath to its hosted plugin. */
    void applyPluginStateAt(const ChainNodePath& devicePath);

    /** @brief Project the internal device at @p devicePath's authored state onto its instance. */
    void projectAuthoredStateAt(const ChainNodePath& devicePath);

    /**
     * @brief Drop entries whose plugins are no longer installed. Returns how many went.
     *
     * Per-entry via doesPluginStillExist so each format decides: a plain File::exists()
     * skips every AU entry, whose fileOrIdentifier is "AudioUnit:..." and not a path. A
     * file-based plugin on an unmounted external volume is kept, since the missing path
     * does not prove it was uninstalled. @p volumeIsMounted is a test seam for that.
     */
    static int pruneMissingPlugins(juce::KnownPluginList& knownPlugins,
                                   juce::AudioPluginFormatManager& formatManager,
                                   std::function<bool(const juce::String&)> volumeIsMounted = {});

    /**
     * @brief Drop entries sharing (path, format) with a fresh descriptor but a uid the scan
     * did not return. Returns how many went.
     *
     * Vendors bump VST3 uniqueIds across major versions and JUCE keys KnownPluginList by
     * (path, deprecatedUid, uniqueId), so old and new coexist on one .vst3 file as a
     * duplicate row for a single installed binary (#1005). Multi-component VST3s (Vital,
     * Kontakt) legitimately expose several uids per path, so every uid this scan returned
     * is kept, and a (path, format) this run did not scan is left alone.
     */
    static int removeSupersededEntries(juce::KnownPluginList& knownPlugins,
                                       const juce::Array<juce::PluginDescription>& freshScan);

    /** @brief Where plugin metadata is stored. */
    static juce::File listFile();

#ifdef MAGDA_ENABLE_TEST_HOOKS
    /// Stands in for a scan the coordinator has live, which needs the out-of-process
    /// scanner a test has no way to drive.
    void testBeginScan() {
        scanning_ = true;
    }
    std::uint64_t testAttachment() const {
        return attachment_;
    }
    bool testWouldAcceptWorkFrom(std::uint64_t attachment) const {
        return attachment == attachment_;
    }
#endif

  private:
    PluginService();
    ~PluginService();

    PluginScanCoordinator& coordinator() const;
    void loadList();
    PluginStateProvider* currentProvider() const {
        return stateProvider_;
    }

    juce::AudioPluginFormatManager* formats_ = nullptr;
    juce::KnownPluginList* list_ = nullptr;

    /// Which attachment the pair above belongs to. Work queued under an earlier one is
    /// dropped rather than applied to whatever is attached when it runs.
    std::uint64_t attachment_ = 0;

    bool scanning_ = false;
    bool metadataLoaded_ = false;
    mutable std::unique_ptr<PluginScanCoordinator> coordinator_;
    std::thread discoveryThread_;

    /// The discovery thread hops back to the message thread; this is what tells it not to.
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    std::function<void(bool, int, const juce::StringArray&)> onScanComplete_;
    std::function<void(const juce::String&)> onScanStatus_;
    std::function<std::vector<ScannedPluginParameter>(const juce::String&)> internalScanner_;
    PluginStateProvider* stateProvider_ = nullptr;
};

}  // namespace magda
