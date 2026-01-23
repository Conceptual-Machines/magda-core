#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../layout/LayoutConfig.hpp"
#include "../../state/TimelineController.hpp"

namespace magda {

/**
 * GridOverlayComponent - Draws vertical time grid lines
 *
 * This component is designed to be used as a background layer that draws
 * vertical grid lines based on time/beat positions. It's transparent to
 * mouse events and can be reused across different views (arrangement,
 * piano roll, automation, etc.).
 *
 * The grid automatically adapts to:
 * - Zoom level (shows more/fewer subdivisions)
 * - Display mode (seconds vs bars/beats)
 * - Tempo and time signature
 */
class GridOverlayComponent : public juce::Component, public TimelineStateListener {
  public:
    GridOverlayComponent();
    ~GridOverlayComponent() override;

    void paint(juce::Graphics& g) override;

    // Transparent to mouse events - clicks pass through
    bool hitTest(int x, int y) override {
        return false;
    }

    // Connect to timeline controller for state updates
    void setController(TimelineController* controller);

    // Manual state setters (for use without controller)
    void setZoom(double zoom);
    void setTimelineLength(double length);
    void setTimeDisplayMode(TimeDisplayMode mode);
    void setTempo(double bpm);
    void setTimeSignature(int numerator, int denominator);

    // Left padding to align with timeline markers
    void setLeftPadding(int padding) {
        leftPadding = padding;
        repaint();
    }
    int getLeftPadding() const {
        return leftPadding;
    }

    // Scroll offset (for drawing grid lines in viewport-relative coordinates)
    void setScrollOffset(int offset) {
        scrollOffset = offset;
        repaint();
    }
    int getScrollOffset() const {
        return scrollOffset;
    }

    // TimelineStateListener implementation
    void timelineStateChanged(const TimelineState& state) override;
    void zoomStateChanged(const TimelineState& state) override;

  private:
    // Controller reference (not owned)
    TimelineController* timelineController = nullptr;

    // Cached state
    double currentZoom = 1.0;
    double timelineLength = 300.0;
    TimeDisplayMode displayMode = TimeDisplayMode::BarsBeats;
    double tempoBPM = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    int leftPadding = LayoutConfig::TIMELINE_LEFT_PADDING;  // Default to match timeline
    int scrollOffset = 0;  // Horizontal scroll offset for viewport-relative drawing

    // Grid drawing methods
    void drawTimeGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void drawSecondsGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void drawBarsBeatsGrid(juce::Graphics& g, juce::Rectangle<int> area);
    void drawBeatOverlay(juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridOverlayComponent)
};

}  // namespace magda
