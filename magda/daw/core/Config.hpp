#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ClipTypes.hpp"

namespace magda {

class ConfigListener {
  public:
    virtual ~ConfigListener() = default;
    virtual void configChanged() = 0;
};

/**
 * Configuration class to manage all configurable settings in the DAW
 * This will later be exposed through a UI for user customization
 */
class Config {
  public:
    static Config& getInstance();

    void addListener(ConfigListener* l) {
        listeners_.push_back(l);
    }
    void removeListener(ConfigListener* l) {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end());
    }

    // Timeline Configuration (stored in bars)
    int getDefaultTimelineLengthBars() const {
        return defaultTimelineLengthBars;
    }
    void setDefaultTimelineLengthBars(int bars) {
        defaultTimelineLengthBars = bars;
    }

    int getDefaultZoomViewBars() const {
        return defaultZoomViewBars;
    }
    void setDefaultZoomViewBars(int bars) {
        defaultZoomViewBars = bars;
    }

    // Zoom Configuration
    double getMinZoomLevel() const {
        return minZoomLevel;
    }
    void setMinZoomLevel(double level) {
        minZoomLevel = level;
    }

    double getMaxZoomLevel() const {
        return maxZoomLevel;
    }
    void setMaxZoomLevel(double level) {
        maxZoomLevel = level;
    }

    // Zoom Sensitivity Configuration
    double getZoomInSensitivity() const {
        return zoomInSensitivity;
    }
    void setZoomInSensitivity(double sensitivity) {
        zoomInSensitivity = sensitivity;
    }

    double getZoomOutSensitivity() const {
        return zoomOutSensitivity;
    }
    void setZoomOutSensitivity(double sensitivity) {
        zoomOutSensitivity = sensitivity;
    }

    double getZoomInSensitivityShift() const {
        return zoomInSensitivityShift;
    }
    void setZoomInSensitivityShift(double sensitivity) {
        zoomInSensitivityShift = sensitivity;
    }

    double getZoomOutSensitivityShift() const {
        return zoomOutSensitivityShift;
    }
    void setZoomOutSensitivityShift(double sensitivity) {
        zoomOutSensitivityShift = sensitivity;
    }

    // Transport Display Configuration
    bool getTransportShowBothFormats() const {
        return transportShowBothFormats;
    }
    void setTransportShowBothFormats(bool show) {
        transportShowBothFormats = show;
    }

    bool getOpenPluginWindowOnDrop() const {
        return openPluginWindowOnDrop;
    }
    void setOpenPluginWindowOnDrop(bool open) {
        openPluginWindowOnDrop = open;
    }

    bool getTransportDefaultBarsBeats() const {
        return transportDefaultBarsBeats;
    }
    void setTransportDefaultBarsBeats(bool useBarsBeats) {
        transportDefaultBarsBeats = useBarsBeats;
    }

    // Panel Visibility Configuration
    bool getShowLeftPanel() const {
        return showLeftPanel;
    }
    void setShowLeftPanel(bool show) {
        showLeftPanel = show;
    }

    bool getShowRightPanel() const {
        return showRightPanel;
    }
    void setShowRightPanel(bool show) {
        showRightPanel = show;
    }

    bool getShowBottomPanel() const {
        return showBottomPanel;
    }
    void setShowBottomPanel(bool show) {
        showBottomPanel = show;
    }

    // Panel Collapse State
    bool getLeftPanelCollapsed() const {
        return leftPanelCollapsed;
    }
    void setLeftPanelCollapsed(bool collapsed) {
        leftPanelCollapsed = collapsed;
    }

    bool getRightPanelCollapsed() const {
        return rightPanelCollapsed;
    }
    void setRightPanelCollapsed(bool collapsed) {
        rightPanelCollapsed = collapsed;
    }

    bool getBottomPanelCollapsed() const {
        return bottomPanelCollapsed;
    }
    void setBottomPanelCollapsed(bool collapsed) {
        bottomPanelCollapsed = collapsed;
    }

    // Panel Sizes (0 = use default)
    int getLeftPanelWidth() const {
        return leftPanelWidth;
    }
    void setLeftPanelWidth(int width) {
        leftPanelWidth = width;
    }

    int getRightPanelWidth() const {
        return rightPanelWidth;
    }
    void setRightPanelWidth(int width) {
        rightPanelWidth = width;
    }

    int getBottomPanelHeight() const {
        return bottomPanelHeight;
    }
    void setBottomPanelHeight(int height) {
        bottomPanelHeight = height;
    }

    // Layout Configuration
    bool getScrollbarOnLeft() const {
        return scrollbarOnLeft;
    }
    void setScrollbarOnLeft(bool onLeft) {
        scrollbarOnLeft = onLeft;
    }

    // When true, the primary-view scrollbars hide on idle and fade in on hover.
    // When false, they are always visible (classic behaviour).
    bool getMainViewScrollbarsAutoHide() const {
        return arrangementScrollbarsAutoHide;
    }
    void setMainViewScrollbarsAutoHide(bool autoHide) {
        arrangementScrollbarsAutoHide = autoHide;
    }

    // UI scale factor for HiDPI displays.
    // 0 = Auto (pick from primary display DPI at startup); >0 = explicit factor (e.g. 1.5).
    double getUIScale() const {
        return uiScale;
    }
    void setUIScale(double scale) {
        uiScale = scale;
    }

    // Built-in UI theme identifier. Theme implementation stays in the UI
    // layer; Config only persists the selected stable identifier.
    const std::string& getTheme() const {
        return theme;
    }
    void setTheme(std::string themeId) {
        theme = themeId.empty() ? "dark" : std::move(themeId);
    }

    // UI spacing density multiplier (1.0 = normal). Scales spacing/padding
    // tokens only (not fonts or widget/track sizes), applied live via the
    // ConfigListener broadcast. Independent from UI scale. Consumed by
    // LayoutConfig / MixerMetrics::applyDensityScale() and densityScaled().
    double getUIDensityScale() const {
        return uiDensityScale;
    }
    void setUIDensityScale(double scale) {
        uiDensityScale = std::clamp(scale, 0.6, 1.4);
    }

    // Font size scale for MAGDA-owned UI fonts. This is independent from
    // Desktop UI scale, which changes both text and component geometry.
    double getUIFontScale() const {
        return uiFontScale;
    }
    void setUIFontScale(double scale) {
        uiFontScale = std::clamp(scale, 0.8, 1.5);
    }

    // UI font family for MAGDA-owned text. Empty = the bundled Inter default;
    // otherwise a system font family name that FontManager resolves.
    const std::string& getUIFontFamily() const {
        return uiFontFamily;
    }
    void setUIFontFamily(std::string family) {
        uiFontFamily = std::move(family);
    }

    // Extra font multiplier for UI text. This compounds with the global UI font scale.
    // English defaults to 100%; locale defaults can seed it higher for denser scripts.
    double getLocalizedUIFontScale() const {
        return localizedUIFontScale;
    }
    void setLocalizedUIFontScale(double scale) {
        localizedUIFontScale = std::clamp(scale, 1.0, 3.0);
        localizedUIFontScaleExplicit = true;
    }

    bool hasExplicitLocalizedUIFontScale() const {
        return localizedUIFontScaleExplicit;
    }

    double getEffectiveUIFontScale() const {
        return uiFontScale * localizedUIFontScale;
    }

    static double defaultLocalizedUIFontScaleForLanguage(const juce::String& languageCode) {
        const auto lang = languageCode.toLowerCase();
        if (lang == "zh" || lang.startsWith("zh-") || lang.startsWith("zh_"))
            return 1.15;
        if (lang == "ja" || lang.startsWith("ja-") || lang.startsWith("ja_"))
            return 1.10;
        return 1.0;
    }

    // Audio Device Configuration
    std::string getPreferredAudioDevice() const {
        return preferredAudioDevice;
    }
    void setPreferredAudioDevice(const std::string& deviceName) {
        preferredAudioDevice = deviceName;
    }

    std::string getPreferredInputDevice() const {
        return preferredInputDevice;
    }
    void setPreferredInputDevice(const std::string& deviceName) {
        preferredInputDevice = deviceName;
    }

    std::string getPreferredOutputDevice() const {
        return preferredOutputDevice;
    }
    void setPreferredOutputDevice(const std::string& deviceName) {
        preferredOutputDevice = deviceName;
    }

    int getPreferredInputChannels() const {
        return preferredInputChannels;
    }
    void setPreferredInputChannels(int channels) {
        preferredInputChannels = channels;
    }

    int getPreferredOutputChannels() const {
        return preferredOutputChannels;
    }
    void setPreferredOutputChannels(int channels) {
        preferredOutputChannels = channels;
    }

    // Custom Plugin Paths
    std::vector<std::string> getCustomPluginPaths() const {
        return customPluginPaths;
    }
    void setCustomPluginPaths(const std::vector<std::string>& paths) {
        customPluginPaths = paths;
    }

    // Hardware ports auto-enabled for External FX / Instrument inserts
    // (owned by ExternalInsertDeviceEnablement, name-keyed). TE persists
    // device enablement globally, so without this set a port MAGDA
    // auto-enabled would come back after a restart looking user-enabled and
    // never be auto-disabled again.
    std::vector<std::string> getAutoEnabledInsertInputs() const {
        return autoEnabledInsertInputs_;
    }
    void setAutoEnabledInsertInputs(const std::vector<std::string>& names) {
        autoEnabledInsertInputs_ = names;
    }
    std::vector<std::string> getAutoEnabledInsertOutputs() const {
        return autoEnabledInsertOutputs_;
    }
    void setAutoEnabledInsertOutputs(const std::vector<std::string>& names) {
        autoEnabledInsertOutputs_ = names;
    }

    // Total plugin count (persisted after last successful scan)
    int getTotalPluginCount() const {
        return totalPluginCount;
    }
    void setTotalPluginCount(int count) {
        totalPluginCount = count;
    }

    // Scan plugins on startup (auto-detect new/removed plugins)
    bool getScanPluginsOnStartup() const {
        return scanPluginsOnStartup;
    }
    void setScanPluginsOnStartup(bool enabled) {
        scanPluginsOnStartup = enabled;
    }

    // Load AI model on startup
    bool getLoadModelOnStartup() const {
        return loadModelOnStartup;
    }
    void setLoadModelOnStartup(bool enabled) {
        loadModelOnStartup = enabled;
    }

    // Transport: when true, pressing Stop snaps the playhead (editPosition)
    // to wherever playback was at the moment of stopping, so the next Play
    // resumes from there. When false (default), the playhead stays put and
    // Play always restarts from the playhead's current location.
    bool getStopUpdatesPlayhead() const {
        return stopUpdatesPlayhead;
    }
    void setStopUpdatesPlayhead(bool enabled) {
        stopUpdatesPlayhead = enabled;
    }

    // Auto-scroll the arrangement to follow the playhead during playback.
    bool getFollowPlayhead() const {
        return followPlayhead;
    }
    void setFollowPlayhead(bool enabled) {
        followPlayhead = enabled;
    }

    // Whether a newly created chord track auditions its progression on playback
    // by default (the chord-track speaker toggle, which is the track's mute).
    bool getChordPreviewOnByDefault() const {
        return chordPreviewOnByDefault;
    }
    void setChordPreviewOnByDefault(bool enabled) {
        chordPreviewOnByDefault = enabled;
    }

    // Whether newly created audio clips get AUTO-XFADE enabled (#1499): their
    // overlaps with other auto-crossfade audio clips play as crossfades
    // instead of being trimmed away.
    bool getAutoCrossfadeByDefault() const {
        return autoCrossfadeByDefault;
    }
    void setAutoCrossfadeByDefault(bool enabled) {
        autoCrossfadeByDefault = enabled;
    }

    // What overlapping arrangement clips sound like (#2003). Default is that
    // the clip on top owns the span it covers, matching what the lane draws.
    ClipOverlapPlayback getClipOverlapPlayback() const {
        return clipOverlapPlaysBoth ? ClipOverlapPlayback::PlayBoth : ClipOverlapPlayback::TopWins;
    }
    void setClipOverlapPlaysBoth(bool playBoth) {
        clipOverlapPlaysBoth = playBoth;
    }
    bool getClipOverlapPlaysBoth() const {
        return clipOverlapPlaysBoth;
    }

    // Recent Projects
    std::vector<std::string> getRecentProjects() const {
        return recentProjects;
    }
    void addRecentProject(const std::string& path);
    void clearRecentProjects() {
        recentProjects.clear();
    }

    // Browser Favorites
    std::vector<std::string> getBrowserFavorites() const {
        return browserFavorites;
    }
    void setBrowserFavorites(const std::vector<std::string>& paths) {
        browserFavorites = paths;
    }

    // Auto-update check
    bool getAutoCheckUpdates() const {
        return autoCheckUpdates;
    }
    void setAutoCheckUpdates(bool enabled) {
        autoCheckUpdates = enabled;
    }
    int64_t getLastUpdateCheckTimestamp() const {
        return lastUpdateCheckTimestamp;
    }
    void setLastUpdateCheckTimestamp(int64_t ms) {
        lastUpdateCheckTimestamp = ms;
    }

    // Browser filter settings
    bool getBrowserFilterAudio() const {
        return browserFilterAudio;
    }
    void setBrowserFilterAudio(bool enabled) {
        browserFilterAudio = enabled;
    }
    bool getBrowserFilterMidi() const {
        return browserFilterMidi;
    }
    void setBrowserFilterMidi(bool enabled) {
        browserFilterMidi = enabled;
    }
    bool getBrowserFilterPreset() const {
        return browserFilterPreset;
    }
    void setBrowserFilterPreset(bool enabled) {
        browserFilterPreset = enabled;
    }

    // Sample browser file view: directory tree instead of a flat list (#1699)
    bool getBrowserTreeView() const {
        return browserTreeView;
    }
    void setBrowserTreeView(bool enabled) {
        browserTreeView = enabled;
    }

    // Browser Default Directory
    std::string getBrowserDefaultDirectory() const {
        return browserDefaultDirectory;
    }
    void setBrowserDefaultDirectory(const std::string& dir) {
        browserDefaultDirectory = dir;
    }

    // Last view the media explorer was on at shutdown ("filesystem" / "library").
    std::string getBrowserLastView() const {
        return browserLastView;
    }
    void setBrowserLastView(const std::string& view) {
        browserLastView = view;
    }

    // User-chosen location for the Sample Tagger ONNX bundle. Empty
    // string = default (dataDir/MediaDB/models). MediaDbContext::modelsDir()
    // returns this when set and the directory exists; falls back to the
    // default otherwise. Lets users keep the ~600 MB bundle on an
    // external drive without symlinking.
    std::string getSampleTaggerModelsDir() const {
        return sampleTaggerModelsDir;
    }
    void setSampleTaggerModelsDir(const std::string& dir) {
        sampleTaggerModelsDir = dir;
    }

    // User-chosen location for the optional command-model ONNX bundle.
    // Empty string = default (dataDir/CommandModel/models).
    std::string getCommandModelModelsDir() const {
        return commandModelModelsDir;
    }
    void setCommandModelModelsDir(const std::string& dir) {
        commandModelModelsDir = dir;
    }

    // Load the Sample Tagger encoders + tokenizer eagerly at app startup
    // instead of waiting for the first DB query that needs them. Eats
    // ~700 MB of RAM and a few seconds of init time, but no first-query
    // hitch later. Off by default.
    bool getLoadSampleTaggerOnStartup() const {
        return loadSampleTaggerOnStartup;
    }
    void setLoadSampleTaggerOnStartup(bool enabled) {
        loadSampleTaggerOnStartup = enabled;
    }

    // Optional override for the media DB directory. Empty string =
    // default (dataDir/MediaDB). MediaDbContext::dbPath() / modelsDir()
    // route through this when set and the directory exists. Lets users
    // park the (potentially large) index on a different drive.
    std::string getMediaDbDir() const {
        return mediaDbDir;
    }
    void setMediaDbDir(const std::string& dir) {
        mediaDbDir = dir;
    }

    // Optional executable/application used by "Edit in External Editor" on audio clips.
    std::string getExternalAudioEditorPath() const {
        return externalAudioEditorPath;
    }
    void setExternalAudioEditorPath(const std::string& path) {
        externalAudioEditorPath = path;
    }

    // Export Audio Configuration
    std::string getExportFormat() const {
        return exportFormat;
    }
    void setExportFormat(const std::string& format) {
        exportFormat = format;
    }

    double getExportSampleRate() const {
        return exportSampleRate;
    }
    void setExportSampleRate(double rate) {
        exportSampleRate = rate;
    }

    // Render Configuration
    std::string getRenderFolder() const {
        return renderFolder;
    }
    void setRenderFolder(const std::string& folder) {
        renderFolder = folder;
    }

    // ----- Configurable user-data paths --------------------------------
    // Empty string means "use OS default". Resolution + env-var override
    // happens in magda::paths (AppPaths.hpp); these are just the persisted
    // override strings.

    /** Override for `userApplicationDataDirectory/MAGDA/` — logs, scripts,
     *  controller profiles, plugin caches. Empty = OS default. Changes
     *  require a restart to fully apply (file logger + plugin scanner
     *  hold open file handles). */
    std::string getDataDir() const {
        return dataDir;
    }
    void setDataDir(const std::string& d) {
        dataDir = d;
    }

    /** Override for `userDocumentsDirectory/MAGDA/Presets/` — Chains, Racks,
     *  Devices. Empty = OS default. Hot-swappable via Config listeners. */
    std::string getPresetsDir() const {
        return presetsDir;
    }
    void setPresetsDir(const std::string& d) {
        presetsDir = d;
    }

    double getRenderSampleRate() const {
        return renderSampleRate;
    }
    void setRenderSampleRate(double rate) {
        renderSampleRate = rate;
    }

    int getRenderBitDepth() const {
        return renderBitDepth;
    }
    void setRenderBitDepth(int depth) {
        renderBitDepth = depth;
    }

    std::string getRenderFilePattern() const {
        return renderFilePattern;
    }
    void setRenderFilePattern(const std::string& pattern) {
        renderFilePattern = pattern;
    }

    std::string getBounceFilePattern() const {
        return bounceFilePattern;
    }
    void setBounceFilePattern(const std::string& pattern) {
        bounceFilePattern = pattern;
    }

    int getBounceBitDepth() const {
        return bounceBitDepth;
    }
    void setBounceBitDepth(int depth) {
        bounceBitDepth = depth;
    }

    // LLM-specific settings. This is one possible inference backend, not the
    // configuration identity of an agent workload.
    struct AgentLLMConfig {
        std::string provider = "openai_chat";
        std::string baseUrl;
        std::string apiKey;
        std::string model;
    };

    // Agent role -> inference profile. New backends can be added without
    // redefining agent roles or turning their configuration into "LLM roles".
    struct AgentInferenceConfig {
        std::string backend = "llm";
        AgentLLMConfig llm;

        bool usesLLM() const {
            return backend == "llm";
        }
    };

    std::string getAIPreset() const {
        return aiPreset;
    }
    void setAIPreset(const std::string& preset) {
        aiPreset = preset;
    }

    AgentInferenceConfig getAgentInferenceConfig(const std::string& agentRole) const {
        auto it = agentInferenceConfigs.find(agentRole);
        if (it != agentInferenceConfigs.end())
            return it->second;
        // Faust and Chord were split out of the Music workload after per-agent
        // configs shipped. Keep pre-migration/in-memory configurations working
        // even before they are saved again with the new explicit keys.
        if (agentRole == "faust" || agentRole == "chord") {
            if (auto music = agentInferenceConfigs.find("music");
                music != agentInferenceConfigs.end())
                return music->second;
        }
        return {};
    }
    void setAgentInferenceConfig(const std::string& agentRole, const AgentInferenceConfig& config) {
        agentInferenceConfigs[agentRole] = config;
    }

    // Temporary backend-specific bridge for existing LLM agents and settings
    // UI. These are not role APIs; callers configuring an agent should use
    // the inference-profile methods above.
    AgentLLMConfig getAgentLLMConfig(const std::string& agentRole) const {
        return getAgentInferenceConfig(agentRole).llm;
    }
    void setAgentLLMConfig(const std::string& agentRole, const AgentLLMConfig& config) {
        auto profile = getAgentInferenceConfig(agentRole);
        profile.backend = "llm";
        profile.llm = config;
        setAgentInferenceConfig(agentRole, profile);
    }

    const std::map<std::string, AgentInferenceConfig>& getAllAgentInferenceConfigs() const {
        return agentInferenceConfigs;
    }

    // Per-provider API credentials (provider name → API key)
    std::string getAICredential(const std::string& provider) const {
        auto it = aiCredentials.find(provider);
        if (it != aiCredentials.end())
            return it->second;
        return {};
    }
    void setAICredential(const std::string& provider, const std::string& key) {
        aiCredentials[provider] = key;
    }
    const std::map<std::string, std::string>& getAllAICredentials() const {
        return aiCredentials;
    }

    /** Resolve the API key for an agent: per-agent key first, then credential by provider. */
    std::string resolveApiKey(const std::string& role) const {
        auto cfg = getAgentLLMConfig(role);
        if (!cfg.apiKey.empty())
            return cfg.apiKey;
        return getAICredential(cfg.provider);
    }

    std::string getLocalLlamaUrl() const {
        return localLlamaUrl;
    }
    void setLocalLlamaUrl(const std::string& url) {
        localLlamaUrl = url;
    }

    // Generic local / OpenAI-compatible HTTP server (LM Studio, Ollama,
    // GPUStack, ...). One server, one model, shared by all agent roles.
    std::string getLocalServerUrl() const {
        return localServerUrl;
    }
    void setLocalServerUrl(const std::string& url) {
        localServerUrl = url;
    }
    std::string getLocalServerApiKey() const {
        return localServerApiKey;
    }
    void setLocalServerApiKey(const std::string& key) {
        localServerApiKey = key;
    }
    std::string getLocalServerModel() const {
        return localServerModel;
    }
    void setLocalServerModel(const std::string& model) {
        localServerModel = model;
    }

    // Local llama-server managed process settings
    std::string getLocalModelPath() const {
        return localModelPath;
    }
    void setLocalModelPath(const std::string& path) {
        localModelPath = path;
    }

    std::string getLocalLlamaBinary() const {
        return localLlamaBinary;
    }
    void setLocalLlamaBinary(const std::string& path) {
        localLlamaBinary = path;
    }

    int getLocalLlamaPort() const {
        return localLlamaPort;
    }
    void setLocalLlamaPort(int port) {
        localLlamaPort = port;
    }

    int getLocalLlamaGpuLayers() const {
        return localLlamaGpuLayers;
    }
    void setLocalLlamaGpuLayers(int layers) {
        localLlamaGpuLayers = layers;
    }

    int getLocalLlamaContextSize() const {
        return localLlamaContextSize;
    }
    void setLocalLlamaContextSize(int size) {
        localLlamaContextSize = size;
    }

    // Legacy accessors — delegate to "music" agent config
    std::string getLLMProvider() const {
        return getAgentLLMConfig("music").provider;
    }
    std::string getLLMBaseUrl() const {
        return getAgentLLMConfig("music").baseUrl;
    }
    std::string getLLMApiKey() const {
        return getAgentLLMConfig("music").apiKey;
    }
    std::string getLLMModel() const {
        return getAgentLLMConfig("music").model;
    }
    std::string getOpenAIApiKey() const {
        return getAgentLLMConfig("music").apiKey;
    }
    std::string getOpenAIModel() const {
        return getAgentLLMConfig("music").model;
    }

    // Legacy setters — write to "music" agent config
    void setLLMProvider(const std::string& p) {
        auto cfg = getAgentLLMConfig("music");
        cfg.provider = p;
        setAgentLLMConfig("music", cfg);
    }
    void setLLMBaseUrl(const std::string& url) {
        auto cfg = getAgentLLMConfig("music");
        cfg.baseUrl = url;
        setAgentLLMConfig("music", cfg);
    }
    void setLLMApiKey(const std::string& key) {
        auto cfg = getAgentLLMConfig("music");
        cfg.apiKey = key;
        setAgentLLMConfig("music", cfg);
    }
    void setLLMModel(const std::string& model) {
        auto cfg = getAgentLLMConfig("music");
        cfg.model = model;
        setAgentLLMConfig("music", cfg);
    }
    void setOpenAIApiKey(const std::string& key) {
        setLLMApiKey(key);
    }
    void setOpenAIModel(const std::string& model) {
        setLLMModel(model);
    }

    // Unified default colour palette (tracks + clips share the same palette)
    struct ColourEntry {
        uint32_t colour;
        const char* name;
    };

    static constexpr std::array<ColourEntry, 8> defaultColourPalette = {{
        {0xFF5588AA, "Blue"},
        {0xFF55AA88, "Teal"},
        {0xFF88AA55, "Green"},
        {0xFFAAAA55, "Yellow"},
        {0xFFAA8855, "Orange"},
        {0xFFAA5555, "Red"},
        {0xFFAA55AA, "Purple"},
        {0xFF5555AA, "Indigo"},
    }};

    static uint32_t getDefaultColour(int index) {
        return defaultColourPalette[static_cast<size_t>(index) % defaultColourPalette.size()]
            .colour;
    }

    // Custom colour palette (user-defined via Preferences)
    struct TrackColourEntry {
        uint32_t colour;
        std::string name;
    };

    std::vector<TrackColourEntry> getTrackColourPalette() const {
        return trackColourPalette;
    }
    void setTrackColourPalette(const std::vector<TrackColourEntry>& palette) {
        trackColourPalette = palette;
    }

    // Clip colour mode: how new clips get their colour
    // 0 = inherit from parent track, 1 = cycle through default palette
    int getClipColourMode() const {
        return clipColourMode;
    }
    void setClipColourMode(int mode) {
        clipColourMode = mode;
    }

    // Track Deletion Configuration
    bool getConfirmTrackDelete() const {
        return confirmTrackDelete;
    }
    void setConfirmTrackDelete(bool confirm) {
        confirmTrackDelete = confirm;
    }

    // Duplicate Loop Range behaviour: true grows the loop to cover the original
    // plus the new copy; false advances the loop onto just the new copy.
    bool getDuplicateLoopGrows() const {
        return duplicateLoopGrows;
    }
    void setDuplicateLoopGrows(bool grows) {
        duplicateLoopGrows = grows;
    }

    // Tooltip Configuration
    bool getShowTooltips() const {
        return showTooltips;
    }
    void setShowTooltips(bool show) {
        showTooltips = show;
    }

    // Auto-monitor selected track
    bool getAutoMonitorSelectedTrack() const {
        return autoMonitorSelectedTrack;
    }
    void setAutoMonitorSelectedTrack(bool enabled) {
        autoMonitorSelectedTrack = enabled;
    }

    // Device chain behaviour
    bool getOpenMacrosOnSelect() const {
        return openMacrosOnSelect;
    }
    void setOpenMacrosOnSelect(bool enabled) {
        openMacrosOnSelect = enabled;
    }

    // Mixer view-toggle rail: per-toggle visibility for the mixer's optional
    // panes. All default off; the user opts in via the left rail.
    bool getMixerShowSends() const {
        return mixerShowSends_;
    }
    void setMixerShowSends(bool v) {
        mixerShowSends_ = v;
    }
    bool getMixerShowRouting() const {
        return mixerShowRouting_;
    }
    void setMixerShowRouting(bool v) {
        mixerShowRouting_ = v;
    }
    bool getMixerShowMonitor() const {
        return mixerShowMonitor_;
    }
    void setMixerShowMonitor(bool v) {
        mixerShowMonitor_ = v;
    }
    bool getMixerShowOscilloscope() const {
        return mixerShowOscilloscope_;
    }
    void setMixerShowOscilloscope(bool v) {
        mixerShowOscilloscope_ = v;
    }
    bool getMixerShowSpectrum() const {
        return mixerShowSpectrum_;
    }
    void setMixerShowSpectrum(bool v) {
        mixerShowSpectrum_ = v;
    }
    bool getMixerShowFxChain() const {
        return mixerShowFxChain_;
    }
    void setMixerShowFxChain(bool v) {
        mixerShowFxChain_ = v;
    }

    // Session view-toggle rail: independent from the mixer rail even where the
    // controls reveal similarly named rows.
    bool getSessionShowSends() const {
        return sessionShowSends_;
    }
    void setSessionShowSends(bool v) {
        sessionShowSends_ = v;
    }
    bool getSessionShowRouting() const {
        return sessionShowRouting_;
    }
    void setSessionShowRouting(bool v) {
        sessionShowRouting_ = v;
    }
    bool getSessionShowMonitor() const {
        return sessionShowMonitor_;
    }
    void setSessionShowMonitor(bool v) {
        sessionShowMonitor_ = v;
    }

    // Legacy config value retained for compatibility with existing config.json
    // files. Mixer-analysis devices are now serialized whenever they exist so
    // per-device mini-visualizer settings survive project save/load.
    bool getPersistMixerAnalysis() const {
        return persistMixerAnalysis_;
    }
    void setPersistMixerAnalysis(bool v) {
        persistMixerAnalysis_ = v;
    }

    // Analysis device defaults: the last-used non-colour settings, applied to
    // every newly created Oscilloscope / Spectrum Analyzer. Trace colour is
    // intentionally per-device only; a device restored from a project keeps its
    // own saved colour in pluginState.
    struct OscilloscopeDefaults {
        float timebaseMs = 10.0f;
    };
    struct SpectrumDefaults {
        int fftOrder = 11;  // 11 = 2048, 12 = 4096
        float slopeDbPerOct = 4.5f;
        float smoothing = 0.5f;
    };

    OscilloscopeDefaults getOscilloscopeDefaults() const {
        return oscilloscopeDefaults_;
    }
    void setOscilloscopeDefaults(const OscilloscopeDefaults& d) {
        oscilloscopeDefaults_ = d;
    }
    SpectrumDefaults getSpectrumDefaults() const {
        return spectrumDefaults_;
    }
    void setSpectrumDefaults(const SpectrumDefaults& d) {
        spectrumDefaults_ = d;
    }

    // Preview output channel (stereo pair offset: 0 = outputs 1-2, 2 = outputs 3-4, etc.)
    int getPreviewOutputChannel() const {
        return previewOutputChannel;
    }
    void setPreviewOutputChannel(int channel) {
        if (channel < 0)
            channel = 0;
        // Snap to even (stereo pair boundary)
        channel &= ~1;
        previewOutputChannel = channel;
    }

    // Language / Localization
    std::string getLanguage() const {
        return language;
    }
    void setLanguage(const std::string& lang) {
        language = lang;
    }

    // Auto-save Configuration
    bool getAutoSaveEnabled() const {
        return autoSaveEnabled;
    }
    void setAutoSaveEnabled(bool enabled) {
        autoSaveEnabled = enabled;
    }

    int getAutoSaveIntervalSeconds() const {
        return autoSaveIntervalSeconds;
    }
    void setAutoSaveIntervalSeconds(int seconds) {
        autoSaveIntervalSeconds = std::max(10, seconds);
    }

    // Parameter aliases (user-global layer, serialized to/from config.json)
    juce::var getParamAliases() const {
        return paramAliases_;
    }
    void setParamAliases(const juce::var& aliases) {
        paramAliases_ = aliases;
    }

    // Controllers (serialized to/from config.json "controllers" key)
    juce::var getControllers() const {
        return controllers_;
    }
    void setControllers(const juce::var& c) {
        controllers_ = c;
    }

    // Lua controller script assignments (serialized to/from config.json "luaScripts" key).
    juce::var getLuaScripts() const {
        return luaScripts_;
    }
    void setLuaScripts(const juce::var& scripts) {
        luaScripts_ = scripts;
    }

    std::string getActiveLuaScript() const {
        return activeLuaScript_;
    }
    void setActiveLuaScript(const std::string& scriptName) {
        activeLuaScript_ = scriptName;
    }

    // Filenames of bundled (factory) Lua scripts the user has explicitly
    // enabled in the Controllers dialog. Bundled scripts not in this list
    // stay hidden from the active scripts list and the auto-load picker;
    // user-imported scripts are unaffected.
    std::vector<std::string> getEnabledFactoryLuaScripts() const {
        return enabledFactoryLuaScripts_;
    }
    void setEnabledFactoryLuaScripts(std::vector<std::string> filenames) {
        enabledFactoryLuaScripts_ = std::move(filenames);
    }

    // Global bindings (serialized to/from config.json "globalBindings" key)
    juce::var getGlobalBindings() const {
        return globalBindings_;
    }
    void setGlobalBindings(const juce::var& b) {
        globalBindings_ = b;
    }

    // User keyboard-shortcut overrides (serialized to/from config.json
    // "keyboardBindings" key). Opaque blob: a string holding the XML produced
    // by juce::KeyPressMappingSet::createXml(); empty/void means "use the
    // code-defined defaults". Owned by the command registry (see #20).
    juce::var getKeyboardBindings() const {
        return keyboardBindings_;
    }
    void setKeyboardBindings(const juce::var& b) {
        keyboardBindings_ = b;
    }

    // User mouse-gesture overrides (serialized to/from config.json
    // "gestureBindings" key). Opaque blob owned by GestureRouter (see #21):
    // GestureRouter::toVar() produces it, loadFromVar() restores it. Void
    // means "use the code-defined defaults".
    juce::var getGestureBindings() const {
        return gestureBindings_;
    }
    void setGestureBindings(const juce::var& b) {
        gestureBindings_ = b;
    }

    // MIDI Learn default scope ("project" or "global"; default is Project).
    // Stored in config.json under "midiLearn" -> "defaultScope".
    // Returns 0 for Global, 1 for Project (mirrors BindingScope enum order).
    // Callers that need the typed enum: cast with static_cast<BindingScope>(raw).
    int getMidiLearnDefaultScopeRaw() const {
        return midiLearnDefaultScope_;
    }
    void setMidiLearnDefaultScopeRaw(int scope) {
        midiLearnDefaultScope_ = scope;
    }

    // MCP Server Configuration
    struct MCPServerConfig {
        std::string name;
        std::string command;
        std::vector<std::string> args;
        bool enabled = true;
    };

    std::vector<MCPServerConfig> getMCPServers() const {
        return mcpServers;
    }
    void setMCPServers(const std::vector<MCPServerConfig>& servers) {
        mcpServers = servers;
    }

    // Save/load to platform-appropriate location:
    //   macOS  ~/Library/Application Support/MAGDA/config.json
    //   Windows  %APPDATA%\MAGDA\config.json
    //   Linux  ~/.config/MAGDA/config.json
    void save();
    void load();

  private:
    Config() = default;

    // Timeline settings (in bars)
    int defaultTimelineLengthBars = 256;  // ~512 seconds at 120 BPM
    int defaultZoomViewBars = 32;         // ~64 seconds at 120 BPM

    // Zoom limits
    double minZoomLevel = 0.01;     // Minimum zoom level (allows extreme zoom out)
    double maxZoomLevel = 10000.0;  // Maximum zoom level (sample-level detail)

    // Zoom sensitivity settings
    double zoomInSensitivity = 25.0;       // Normal zoom-in sensitivity
    double zoomOutSensitivity = 40.0;      // Normal zoom-out sensitivity
    double zoomInSensitivityShift = 8.0;   // Shift+zoom-in sensitivity (more aggressive)
    double zoomOutSensitivityShift = 8.0;  // Shift+zoom-out sensitivity (more aggressive)

    // Transport display settings
    bool transportShowBothFormats = false;  // Show both bars/beats and seconds

    // Open a device's editor window automatically when it is dropped into a chain
    bool openPluginWindowOnDrop = false;
    bool transportDefaultBarsBeats = true;  // Default to bars/beats (false = seconds)

    // Panel visibility settings
    bool showLeftPanel = true;    // Show left panel by default
    bool showRightPanel = true;   // Show right panel by default
    bool showBottomPanel = true;  // Show bottom panel by default

    // Panel collapse state (persisted across sessions)
    bool leftPanelCollapsed = false;
    bool rightPanelCollapsed = false;
    bool bottomPanelCollapsed = false;

    // Panel sizes (0 = use LayoutConfig default)
    int leftPanelWidth = 0;
    int rightPanelWidth = 0;
    int bottomPanelHeight = 0;

    // Track deletion settings
    bool confirmTrackDelete = true;  // Show confirmation dialog before deleting a track

    // Duplicate Loop Range: grow the loop over the copy (true) or advance onto it (false)
    bool duplicateLoopGrows = true;

    // Tooltip settings
    bool showTooltips = true;  // Enabled by default — disable via config

    // Auto-monitor settings
    bool autoMonitorSelectedTrack = false;  // Auto-enable input monitor on selected track

    // Device chain behaviour
    bool openMacrosOnSelect = true;  // Open macro panel when selecting a device/rack

    // Mixer view-toggle rail (default all off; users opt in via the rail)
    bool mixerShowSends_ = false;
    bool mixerShowRouting_ = false;
    bool mixerShowMonitor_ = false;
    bool mixerShowOscilloscope_ = false;
    bool mixerShowSpectrum_ = false;
    bool mixerShowFxChain_ = false;
    bool sessionShowSends_ = false;
    bool sessionShowRouting_ = false;
    bool sessionShowMonitor_ = false;
    bool persistMixerAnalysis_ = false;

    // Analysis device last-used defaults (see getters above).
    OscilloscopeDefaults oscilloscopeDefaults_;
    SpectrumDefaults spectrumDefaults_;

    // Auto-save settings
    bool autoSaveEnabled = true;       // Auto-save enabled by default
    int autoSaveIntervalSeconds = 60;  // Save every 60 seconds

    // Preview output channel (stereo pair offset: 0 = outputs 1-2, 2 = outputs 3-4, etc.)
    int previewOutputChannel = 0;

    // Clip colour mode: 0 = inherit from parent track, 1 = cycle through default palette
    int clipColourMode = 0;

    // Custom colour palette (ARGB hex + display name, user-defined via Preferences)
    std::vector<TrackColourEntry> trackColourPalette;

    // Layout settings
    bool scrollbarOnLeft = false;               // Scrollbar on right by default
    bool arrangementScrollbarsAutoHide = true;  // Hover-reveal scrollbars by default

    // UI scale: 0 = Auto (pick from display DPI), otherwise an explicit factor (1.0, 1.25, …)
    double uiScale = 0.0;

    // Runtime theme selection. "dark" is deliberately the compatibility
    // default for configurations written before themes existed.
    std::string theme = "dark";

    // UI spacing density multiplier (1.0 = normal). Clamped to [0.6, 1.4].
    double uiDensityScale = 1.0;

    // UI font scale: multiplier applied by FontManager to app-owned text fonts.
    double uiFontScale = 1.0;

    // UI font family: empty = bundled Inter default; else a system family name.
    std::string uiFontFamily;

    // Localized UI font scale: additional multiplier for non-English UI languages.
    double localizedUIFontScale = 1.0;
    bool localizedUIFontScaleExplicit = false;

    // Recent projects (most recent first, max 10)
    std::vector<std::string> recentProjects;

    // Custom plugin paths
    std::vector<std::string> customPluginPaths;

    // Ports auto-enabled by ExternalInsertDeviceEnablement (see accessors)
    std::vector<std::string> autoEnabledInsertInputs_;
    std::vector<std::string> autoEnabledInsertOutputs_;

    // Total plugin count from last scan
    int totalPluginCount = 0;

    // Auto-detect new plugins on startup (off by default)
    bool scanPluginsOnStartup = false;

    // Load AI model on startup (off by default)
    bool loadModelOnStartup = false;

    // See getStopUpdatesPlayhead — default keeps the playhead in place
    // across Stop/Play cycles (Bitwig-style "play from playhead").
    bool stopUpdatesPlayhead = false;

    // Auto-scroll the arrangement to follow the playhead during playback.
    bool followPlayhead = true;

    // Audition a new chord track's progression on playback by default.
    bool chordPreviewOnByDefault = false;

    // New audio clips get AUTO-XFADE enabled (see #1499).
    bool autoCrossfadeByDefault = true;

    // Overlapping arrangement clips stack and all of them play, instead of the
    // clip on top owning the span it covers (see #2003).
    bool clipOverlapPlaysBoth = false;

    // Browser filter settings (media explorer)
    bool browserFilterAudio = true;    // Show audio files by default
    bool browserFilterMidi = false;    // Hide MIDI files by default
    bool browserFilterPreset = false;  // Hide MAGDA presets by default
    bool browserTreeView = false;      // Sample browser: tree view instead of flat list (#1699)

    // Browser favorites and default directory
    std::vector<std::string> browserFavorites;
    std::string browserDefaultDirectory = "";  // empty = user home

    // Which view the media explorer should restore on startup.
    // "filesystem" → file browser at browserDefaultDirectory.
    // "library"    → DB browser (sample library).
    std::string browserLastView = "filesystem";

    // Optional override for the Sample Tagger ONNX bundle location.
    // Empty = use the default dataDir/MediaDB/models.
    std::string sampleTaggerModelsDir = "";

    // Optional override for the command-model ONNX bundle location.
    // Empty = use the default dataDir/CommandModel/models.
    std::string commandModelModelsDir = "";

    // Eagerly load the Sample Tagger encoders + tokenizer at startup
    // (vs lazy on first query).
    bool loadSampleTaggerOnStartup = false;

    // Optional override for the media DB directory. Empty = default
    // (dataDir/MediaDB).
    std::string mediaDbDir = "";

    // External sample editor executable/application path.
    std::string externalAudioEditorPath = "";

    // Auto-update check
    bool autoCheckUpdates = true;          // Check GitHub for newer releases on startup
    int64_t lastUpdateCheckTimestamp = 0;  // ms since epoch; rate-limit at 24h

    // Export audio settings
    std::string exportFormat = "WAV24";  // WAV16, WAV24, WAV32, FLAC
    double exportSampleRate = 48000.0;   // 44100, 48000, 96000, 192000

    // Configurable user-data path overrides (resolved by magda::paths).
    // Empty = OS default. Persisted in config.json.
    std::string dataDir = "";     // userApplicationDataDirectory/MAGDA/
    std::string presetsDir = "";  // userDocumentsDirectory/MAGDA/Presets/

    // Render settings
    std::string renderFolder = "";  // Custom render output folder (empty = renders/ beside source)
    double renderSampleRate = 44100.0;  // 44100, 48000, 96000, 192000
    int renderBitDepth = 24;            // 16, 24, 32
    // File naming pattern tokens: <project-name>, <clip-name>, <track-name>, <date-time>
    std::string renderFilePattern = "<project-name>_<date-time>";
    std::string bounceFilePattern = "<clip-name>_<date-time>";
    int bounceBitDepth = 32;  // 16, 24, 32 — default 32-bit for internal bounces

    // Audio device settings
    std::string preferredAudioDevice = "";   // Preferred audio interface (empty = system default)
    std::string preferredInputDevice = "";   // Preferred input device (empty = system default)
    std::string preferredOutputDevice = "";  // Preferred output device (empty = system default)
    int preferredInputChannels = 0;   // Preferred input channel count (0 = use device default)
    int preferredOutputChannels = 0;  // Preferred output channel count (0 = use device default)

    // Language
    std::string language = "en";  // Language code, matches lang/<code>.json

    // AI settings
    std::string aiPreset = "local_embedded";
    std::map<std::string, AgentInferenceConfig> agentInferenceConfigs = {
        {"command", {"llm", {"llama_local", "", "", ""}}},
        {"music", {"llm", {"llama_local", "", "", ""}}},
        {"faust", {"llm", {"llama_local", "", "", ""}}},
        {"chord", {"llm", {"llama_local", "", "", ""}}},
        {"controller", {"llm", {"llama_local", "", "", ""}}},
        {"theme", {"llm", {"llama_local", "", "", ""}}},
    };
    std::map<std::string, std::string> aiCredentials;  // provider → API key
    std::string localLlamaUrl = "http://127.0.0.1:8080/v1";
    // Generic local / OpenAI-compatible HTTP server settings.
    std::string localServerUrl = "http://localhost:11434/v1";
    std::string localServerApiKey;  // optional bearer token (GPUStack etc.)
    std::string localServerModel;   // opaque model id from /v1/models
    std::string localModelPath;
    std::string localLlamaBinary;  // empty = search PATH
    int localLlamaPort = 8080;
    int localLlamaGpuLayers = -1;  // -1 = auto
    int localLlamaContextSize = 4096;

    // MCP server configs
    std::vector<MCPServerConfig> mcpServers;

    std::vector<ConfigListener*> listeners_;

    // MIDI Learn default scope: 0 = Global, 1 = Project
    // Default is Project (1) to keep bindings per-song by default.
    int midiLearnDefaultScope_ = 1;

    // User-global parameter aliases (opaque JSON blob, managed by AliasRegistry)
    juce::var paramAliases_;

    // Controller devices (opaque JSON blob, managed by ControllerRegistry)
    juce::var controllers_;

    // Lua controller scripts (opaque JSON blob, managed by scripting_app)
    juce::var luaScripts_;
    std::string activeLuaScript_;
    std::vector<std::string> enabledFactoryLuaScripts_;

    // Global bindings (opaque JSON blob, managed by BindingRegistry)
    juce::var globalBindings_;

    // User keyboard-shortcut overrides (opaque KeyPressMappingSet XML string,
    // managed by the command registry; see #20)
    juce::var keyboardBindings_;

    // User mouse-gesture overrides (opaque JSON blob, managed by GestureRouter;
    // see #21)
    juce::var gestureBindings_;
};

}  // namespace magda
