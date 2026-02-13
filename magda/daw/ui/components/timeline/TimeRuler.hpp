#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * Time ruler component displaying time markers and labels.
 * Supports both time-based (seconds) and musical (bars/beats) display modes.
 */
class TimeRuler : public juce::Component, private juce::Timer {
  public:
    enum class DisplayMode { Seconds, BarsBeats };

    TimeRuler();
    ~TimeRuler() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Configuration
    void setZoom(double pixelsPerBeat);
    void setTimelineLength(double lengthInSeconds);
    void setDisplayMode(DisplayMode mode);
    void setScrollOffset(int offsetPixels);

    // For bars/beats mode
    void setTempo(double bpm);
    double getTempo() const {
        return tempo;
    }
    void setTimeSignature(int numerator, int denominator);
    int getTimeSigNumerator() const {
        return timeSigNumerator;
    }

    // Time offset for piano roll (absolute vs relative mode)
    // When set, displayed times are offset by this amount (e.g., clip starts at bar 5)
    void setTimeOffset(double offsetSeconds);
    double getTimeOffset() const {
        return timeOffset;
    }

    // Bar origin offset (shifts bar 1 to a different time position)
    void setBarOrigin(double originSeconds);
    double getBarOrigin() const {
        return barOriginSeconds;
    }

    // For relative mode display (shows 1, 2, 3... instead of 5, 6, 7...)
    void setRelativeMode(bool relative);
    bool isRelativeMode() const {
        return relativeMode;
    }

    // Clip boundary marker (shows where clip content starts/ends)
    void setClipLength(double lengthSeconds);
    double getClipLength() const {
        return clipLength;
    }
    void setClipContentOffset(double offsetSeconds);
    double getClipContentOffset() const {
        return clipContentOffset;
    }

    // Loop region markers (shows loop boundaries on the ruler)
    // enabled: show markers at all; active: loop is actually on (green vs grey)
    void setLoopRegion(double offsetSeconds, double lengthSeconds, bool enabled,
                       bool active = true);

    // Loop phase marker (shows where playback phase is within the loop)
    void setLoopPhaseMarker(double positionSeconds, bool visible);

    // Playhead position (for drawing playhead line during playback)
    void setPlayheadPosition(double positionSeconds);
    double getPlayheadPosition() const {
        return playheadPosition;
    }

    // Left padding (for alignment - can be set to 0 for piano roll)
    void setLeftPadding(int padding);
    int getLeftPadding() const {
        return leftPadding;
    }

    // Link to a viewport for real-time scroll sync
    void setLinkedViewport(juce::Viewport* viewport);
    juce::Viewport* getLinkedViewport() const {
        return linkedViewport;
    }

    // Get preferred height (from LayoutConfig)
    int getPreferredHeight() const;

    // Mouse interaction - click to set playhead, drag to zoom, wheel to scroll
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    // Callbacks
    std::function<void(double)> onPositionClicked;           // Time position clicked
    std::function<void(double, double, int)> onZoomChanged;  // newZoom, anchorTime, anchorScreenX
    std::function<void(int)> onScrollRequested;              // deltaX scroll amount

  private:
    // Display state
    DisplayMode displayMode = DisplayMode::Seconds;
    double zoom = 10.0;             // pixels per beat
    double timelineLength = 300.0;  // seconds
    int scrollOffset = 0;           // pixels

    // Musical time settings
    double tempo = 120.0;  // BPM
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    // Offset and relative mode (for piano roll)
    double timeOffset = 0.0;        // seconds - absolute position of content start
    bool relativeMode = false;      // true = show relative time (1, 2, 3...), false = show absolute
    double barOriginSeconds = 0.0;  // time position where bar 1 starts
    double clipLength = 0.0;        // seconds - length of clip (0 = no boundary marker)
    double clipContentOffset =
        0.0;  // seconds - source offset in timeline seconds (shifts boundaries)
    double playheadPosition = -1.0;  // seconds - current playback position (-1 = not playing)

    // Loop region
    double loopOffset = 0.0;   // seconds - loop start offset within clip
    double loopLength = 0.0;   // seconds - loop length
    bool loopEnabled = false;  // whether loop markers are visible
    bool loopActive = false;   // whether loop is actually enabled (green vs grey)

    // Loop phase marker
    double loopPhasePosition = 0.0;  // phase marker position in timeline seconds
    bool loopPhaseVisible = false;

    // Layout
    int leftPadding = 18;  // Configurable padding (default 18 for main timeline)
    juce::Viewport* linkedViewport = nullptr;  // For real-time scroll sync
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

    // Timer callback for real-time scroll sync
    void timerCallback() override;
    int lastViewportX = 0;  // Track last position to detect changes

    // Drag state (zoom or scroll)
    enum class DragMode { None, Zooming, Scrolling };
    DragMode dragMode = DragMode::None;
    int mouseDownX = 0;
    int mouseDownY = 0;
    int lastDragX = 0;
    double zoomStartValue = 0.0;
    double zoomAnchorTime = 0.0;
    static constexpr int DRAG_THRESHOLD = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeRuler)
};

}  // namespace magda
