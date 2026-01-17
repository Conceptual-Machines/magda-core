#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../../layout/LayoutConfig.hpp"
#include "../../themes/DarkTheme.hpp"

namespace magica {

/**
 * Time ruler component displaying time markers and labels.
 * Supports both time-based (seconds) and musical (bars/beats) display modes.
 */
class TimeRuler : public juce::Component {
  public:
    enum class DisplayMode { Seconds, BarsBeats };

    TimeRuler();
    ~TimeRuler() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Configuration
    void setZoom(double pixelsPerSecond);
    void setTimelineLength(double lengthInSeconds);
    void setDisplayMode(DisplayMode mode);
    void setScrollOffset(int offsetPixels);

    // For bars/beats mode
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);

    // Get preferred height (from LayoutConfig)
    int getPreferredHeight() const;

    // Mouse interaction - click to set playhead
    void mouseDown(const juce::MouseEvent& event) override;

    // Callbacks
    std::function<void(double)> onPositionClicked;  // Time position clicked

  private:
    // Display state
    DisplayMode displayMode = DisplayMode::Seconds;
    double zoom = 20.0;             // pixels per second
    double timelineLength = 300.0;  // seconds
    int scrollOffset = 0;           // pixels

    // Musical time settings
    double tempo = 120.0;  // BPM
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    // Layout
    static constexpr int LEFT_PADDING = 18;
    static constexpr int TICK_HEIGHT_MAJOR = 12;
    static constexpr int TICK_HEIGHT_MINOR = 6;
    static constexpr int LABEL_MARGIN = 4;

    // Drawing helpers
    void drawSecondsMode(juce::Graphics& g);
    void drawBarsBeatsMode(juce::Graphics& g);
    double calculateMarkerInterval() const;
    juce::String formatTimeLabel(double time, double interval) const;
    juce::String formatBarsBeatsLabel(double time) const;

    // Coordinate conversion
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeRuler)
};

}  // namespace magica
