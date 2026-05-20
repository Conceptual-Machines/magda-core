#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

    // UI scale factor for HiDPI displays.
    // 0 = Auto (pick from primary display DPI at startup); >0 = explicit factor (e.g. 1.5).
    double getUIScale() const {
        return uiScale;
    }
    void setUIScale(double scale) {
        uiScale = scale;
    }

    // Font size scale for MAGDA-owned UI fonts. This is independent from
    // Desktop UI scale, which changes both text and component geometry.
    double getUIFontScale() const {
        return uiFontScale;
    }
    void setUIFontScale(double scale) {
        uiFontScale = std::clamp(scale, 0.8, 1.5);
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

    // AI Configuration — per-agent LLM settings
    struct AgentLLMConfig {
        std::string provider = "openai_chat";
        std::string baseUrl;
        std::string apiKey;
        std::string model;
    };

    std::string getAIPreset() const {
        return aiPreset;
    }
    void setAIPreset(const std::string& preset) {
        aiPreset = preset;
    }

    AgentLLMConfig getAgentLLMConfig(const std::string& role) const {
        auto it = agentConfigs.find(role);
        if (it != agentConfigs.end())
            return it->second;
        return {};
    }
    void setAgentLLMConfig(const std::string& role, const AgentLLMConfig& config) {
        agentConfigs[role] = config;
    }

    const std::map<std::string, AgentLLMConfig>& getAllAgentConfigs() const {
        return agentConfigs;
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
        agentConfigs["music"].provider = p;
    }
    void setLLMBaseUrl(const std::string& url) {
        agentConfigs["music"].baseUrl = url;
    }
    void setLLMApiKey(const std::string& key) {
        agentConfigs["music"].apiKey = key;
    }
    void setLLMModel(const std::string& model) {
        agentConfigs["music"].model = model;
    }
    void setOpenAIApiKey(const std::string& key) {
        agentConfigs["music"].apiKey = key;
    }
    void setOpenAIModel(const std::string& model) {
        agentConfigs["music"].model = model;
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

    // Tooltip settings
    bool showTooltips = true;  // Enabled by default — disable via config

    // Auto-monitor settings
    bool autoMonitorSelectedTrack = false;  // Auto-enable input monitor on selected track

    // Device chain behaviour
    bool openMacrosOnSelect = true;  // Open macro panel when selecting a device/rack

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
    bool scrollbarOnLeft = false;  // Scrollbar on right by default

    // UI scale: 0 = Auto (pick from display DPI), otherwise an explicit factor (1.0, 1.25, …)
    double uiScale = 0.0;

    // UI font scale: multiplier applied by FontManager to app-owned text fonts.
    double uiFontScale = 1.0;

    // Recent projects (most recent first, max 10)
    std::vector<std::string> recentProjects;

    // Custom plugin paths
    std::vector<std::string> customPluginPaths;

    // Total plugin count from last scan
    int totalPluginCount = 0;

    // Auto-detect new plugins on startup (off by default)
    bool scanPluginsOnStartup = false;

    // Load AI model on startup (off by default)
    bool loadModelOnStartup = false;

    // See getStopUpdatesPlayhead — default keeps the playhead in place
    // across Stop/Play cycles (Bitwig-style "play from playhead").
    bool stopUpdatesPlayhead = false;

    // Browser filter settings (media explorer)
    bool browserFilterAudio = true;    // Show audio files by default
    bool browserFilterMidi = false;    // Hide MIDI files by default
    bool browserFilterPreset = false;  // Hide MAGDA presets by default

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
    std::map<std::string, AgentLLMConfig> agentConfigs = {
        {"router", {"llama_local", "", "", ""}},
        {"command", {"llama_local", "", "", ""}},
        {"music", {"llama_local", "", "", ""}},
        {"controller", {"llama_local", "", "", ""}},
    };
    std::map<std::string, std::string> aiCredentials;  // provider → API key
    std::string localLlamaUrl = "http://127.0.0.1:8080/v1";
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
};

}  // namespace magda
