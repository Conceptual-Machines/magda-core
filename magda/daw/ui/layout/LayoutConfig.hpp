#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * Layout configuration for the DAW UI.
 * All layout constants in one place, can be modified at runtime.
 *
 * Debug: Press F11 in the app to toggle layout debug overlay.
 */
struct LayoutConfig {
    // Timeline area heights
    int chordRowHeight = 0;         // Chord row disabled (now in piano roll only)
    int arrangementBarHeight = 18;  // Reduced to give more space for time labels
    int timeRulerHeight = 52;       // Increased to accommodate labels

    // Time ruler details
    int rulerMajorTickHeight = 14;  // Shortened to avoid overlap with loop markers
    int rulerMinorTickHeight = 6;   // Shortened to avoid overlap with loop markers
    int rulerLabelFontSize = 11;
    int rulerLabelTopMargin = 10;  // Space between separator line and time labels

    // Ruler interaction zones (fraction of ruler height for zoom area, rest is time selection)
    float rulerZoomAreaRatio = 0.67f;  // Upper 67% for zoom, lower 33% for time selection

    // Helper to get the Y position that splits zoom/selection zones
    int getRulerZoneSplitY() const {
        return chordRowHeight + arrangementBarHeight +
               static_cast<int>(timeRulerHeight * rulerZoomAreaRatio);
    }

    // Grid/tick spacing - shared between timeline ruler and track content grid
    int minGridPixelSpacing = 50;  // Minimum pixels between grid lines/ticks

    // Debug mode
    bool showDebugOverlay = false;

    // Computed total timeline height
    int getTimelineHeight() const {
        return chordRowHeight + arrangementBarHeight + timeRulerHeight;
    }

    // Track layout
    int defaultTrackHeight = 80;
    int minTrackHeight = 40;
    int maxTrackHeight = 200;

    // Track headers
    int defaultTrackHeaderWidth = 200;
    int minTrackHeaderWidth = 150;
    int maxTrackHeaderWidth = 350;

    // Spacing and padding
    int headerContentPadding = 8;
    int componentSpacing = 4;
    int panelPadding = 8;

    // Timeline content left padding - shared across timeline, track content, automation lanes
    static constexpr int TIMELINE_LEFT_PADDING = 23;

    // Zoom controls
    int zoomButtonSize = 24;
    int zoomSliderMinWidth = 60;

    // Main window panels
    int defaultTransportHeight = 60;
    int minTransportHeight = 40;
    int maxTransportHeight = 120;

    int footerHeight = 40;

    int defaultLeftPanelWidth = 250;
    int defaultRightPanelWidth = 300;
    int minPanelWidth = 200;
    int collapsedPanelWidth = 24;
    int panelCollapseThreshold = 50;

    int defaultBottomPanelHeight = 200;
    int minBottomPanelHeight = 100;

    int resizeHandleSize = 3;

    // Toggle debug overlay (F11)
    void toggleDebugOverlay() {
        showDebugOverlay = !showDebugOverlay;
    }

    // Get debug info string for overlay
    juce::String getDebugInfo() const {
        juce::String info;
        info << "=== LayoutConfig ===\n";
        info << "Timeline Total: " << getTimelineHeight() << "px\n";
        info << "  chordRowHeight: " << chordRowHeight << "\n";
        info << "  arrangementBarHeight: " << arrangementBarHeight << "\n";
        info << "  timeRulerHeight: " << timeRulerHeight << "\n";
        info << "Ruler Ticks:\n";
        info << "  majorTickHeight: " << rulerMajorTickHeight << "\n";
        info << "  minorTickHeight: " << rulerMinorTickHeight << "\n";
        info << "  labelFontSize: " << rulerLabelFontSize << "\n";
        info << "Track:\n";
        info << "  defaultHeight: " << defaultTrackHeight << "\n";
        info << "  headerWidth: " << defaultTrackHeaderWidth << "\n";
        return info;
    }

    // Singleton access (for convenience, but components can also receive config via constructor)
    static LayoutConfig& getInstance() {
        static LayoutConfig instance;
        return instance;
    }

  private:
    LayoutConfig() = default;
};

}  // namespace magda
