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

    // Timeline Configuration
    double getDefaultTimelineLength() const {
        return defaultTimelineLength;
    }
    void setDefaultTimelineLength(double length) {
        defaultTimelineLength = length;
    }

    double getDefaultZoomViewDuration() const {
        return defaultZoomViewDuration;
    }
    void setDefaultZoomViewDuration(double duration) {
        defaultZoomViewDuration = duration;
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

    // Save/Load Configuration (for future use)
    void saveToFile(const std::string& filename);
    void loadFromFile(const std::string& filename);

  private:
    Config() = default;

    // Helper to parse a single config line
    void parseConfigLine(const std::string& key, const std::string& value);

    // Timeline settings
    double defaultTimelineLength = 300.0;   // 5 minutes in seconds
    double defaultZoomViewDuration = 60.0;  // Show 1 minute by default

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

    // Layout settings
    bool scrollbarOnLeft = false;  // Scrollbar on right by default

    // Custom plugin paths
    std::vector<std::string> customPluginPaths;

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
