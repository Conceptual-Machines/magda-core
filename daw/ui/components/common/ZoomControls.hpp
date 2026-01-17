#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../../themes/DarkTheme.hpp"

namespace magica {

/**
 * Zoom controls component with +/- buttons and a slider
 * Designed to be placed in the timeline header area
 */
class ZoomControls : public juce::Component {
  public:
    ZoomControls();
    ~ZoomControls() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Set current zoom level (0.0 to 1.0 normalized)
    void setZoomLevel(double normalizedZoom);

    // Get current zoom level (0.0 to 1.0 normalized)
    double getZoomLevel() const;

    // Set the actual zoom range for display/calculation
    void setZoomRange(double minZoom, double maxZoom);

    // Callbacks
    std::function<void(double)> onZoomChanged;  // Called with normalized zoom (0-1)
    std::function<void()> onZoomIn;             // Called when + button clicked
    std::function<void()> onZoomOut;            // Called when - button clicked

  private:
    // UI Components
    juce::TextButton zoomOutButton;
    juce::TextButton zoomInButton;
    juce::Slider zoomSlider;

    // Zoom range
    double minZoom = 0.1;
    double maxZoom = 10000.0;

    // Button click handlers
    void handleZoomOut();
    void handleZoomIn();

    // Slider change handler
    void handleSliderChange();

    // Styling
    void setupButton(juce::TextButton& button, const juce::String& text);
    void setupSlider();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomControls)
};

}  // namespace magica
