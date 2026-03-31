#pragma once

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

    // Browser Default Directory
    std::string getBrowserDefaultDirectory() const {
        return browserDefaultDirectory;
    }
    void setBrowserDefaultDirectory(const std::string& dir) {
        browserDefaultDirectory = dir;
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

    // Browser favorites and default directory
    std::vector<std::string> browserFavorites;
    std::string browserDefaultDirectory = "";  // empty = user home

    // Export audio settings
    std::string exportFormat = "WAV24";  // WAV16, WAV24, WAV32, FLAC
    double exportSampleRate = 48000.0;   // 44100, 48000, 96000, 192000

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

    // AI settings
    std::string aiPreset = "local_embedded";
    std::map<std::string, AgentLLMConfig> agentConfigs = {
        {"router", {"llama_local", "", "", ""}},
        {"command", {"llama_local", "", "", ""}},
        {"music", {"llama_local", "", "", ""}},
    };
    std::map<std::string, std::string> aiCredentials;  // provider → API key
    std::string localLlamaUrl = "http://127.0.0.1:8080/v1";
    std::string localModelPath;
    std::string localLlamaBinary;  // empty = search PATH
    int localLlamaPort = 8080;
    int localLlamaGpuLayers = -1;  // -1 = auto
    int localLlamaContextSize = 4096;

    std::vector<ConfigListener*> listeners_;
};

}  // namespace magda
