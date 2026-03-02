#pragma once

#include <memory>
#include <string>
#include <vector>

namespace magda {

/**
 * Configuration class to manage all configurable settings in the DAW
 * This will later be exposed through a UI for user customization
 */
class Config {
  public:
    static Config& getInstance();

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

    // AI Configuration
    std::string getOpenAIApiKey() const {
        return openaiApiKey;
    }
    void setOpenAIApiKey(const std::string& key) {
        openaiApiKey = key;
    }

    std::string getOpenAIModel() const {
        return openaiModel;
    }
    void setOpenAIModel(const std::string& model) {
        openaiModel = model;
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

    // Preview output channel (stereo pair offset: 0 = outputs 1-2, 2 = outputs 3-4, etc.)
    int previewOutputChannel = 0;

    // Layout settings
    bool scrollbarOnLeft = false;  // Scrollbar on right by default

    // Recent projects (most recent first, max 10)
    std::vector<std::string> recentProjects;

    // Custom plugin paths
    std::vector<std::string> customPluginPaths;

    // Browser favorites and default directory
    std::vector<std::string> browserFavorites;
    std::string browserDefaultDirectory = "";  // empty = user home

    // Export audio settings
    std::string exportFormat = "WAV24";  // WAV16, WAV24, WAV32, FLAC
    double exportSampleRate = 48000.0;   // 44100, 48000, 96000, 192000

    // Render settings
    std::string renderFolder = "";  // Custom render output folder (empty = renders/ beside source)

    // Audio device settings
    std::string preferredAudioDevice = "";   // Preferred audio interface (empty = system default)
    std::string preferredInputDevice = "";   // Preferred input device (empty = system default)
    std::string preferredOutputDevice = "";  // Preferred output device (empty = system default)
    int preferredInputChannels = 0;   // Preferred input channel count (0 = use device default)
    int preferredOutputChannels = 0;  // Preferred output channel count (0 = use device default)

    // AI settings
    std::string openaiApiKey = "";        // OpenAI API key (empty = not configured)
    std::string openaiModel = "gpt-5.2";  // OpenAI model for DSL generation (requires CFG support)
};

}  // namespace magda
