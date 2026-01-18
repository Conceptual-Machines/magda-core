#pragma once

#include <memory>

namespace magica {

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

    // Save/Load Configuration (for future use)
    void saveToFile(const std::string& filename);
    void loadFromFile(const std::string& filename);

  private:
    Config() = default;

    // Timeline settings
    double defaultTimelineLength = 300.0;   // 5 minutes in seconds
    double defaultZoomViewDuration = 60.0;  // Show 1 minute by default

    // Zoom limits
    double minZoomLevel = 0.1;      // Minimum zoom level
    double maxZoomLevel = 10000.0;  // Maximum zoom level (sample-level detail)

    // Zoom sensitivity settings
    double zoomInSensitivity = 25.0;       // Normal zoom-in sensitivity
    double zoomOutSensitivity = 40.0;      // Normal zoom-out sensitivity
    double zoomInSensitivityShift = 8.0;   // Shift+zoom-in sensitivity (more aggressive)
    double zoomOutSensitivityShift = 8.0;  // Shift+zoom-out sensitivity (more aggressive)

    // Transport display settings
    bool transportShowBothFormats = false;  // Show both bars/beats and seconds
    bool transportDefaultBarsBeats = true;  // Default to bars/beats (false = seconds)
};

}  // namespace magica
