#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magica {

/**
 * Layout configuration for the DAW UI.
 * All layout constants in one place, can be modified at runtime.
 *
 * Debug: Press F11 in the app to toggle layout debug overlay.
 */
struct LayoutConfig {
    // Timeline area heights
    int arrangementBarHeight = 30;
    int timeRulerHeight = 40;

    // Time ruler details
    int rulerMajorTickHeight = 16;
    int rulerMinorTickHeight = 8;
    int rulerLabelFontSize = 11;
    int rulerLabelTopMargin = 4;

    // Debug mode
    bool showDebugOverlay = false;

    // Computed total timeline height
    int getTimelineHeight() const {
        return arrangementBarHeight + timeRulerHeight;
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

    // Zoom controls
    int zoomButtonSize = 24;
    int zoomSliderMinWidth = 60;

    // Toggle debug overlay (F11)
    void toggleDebugOverlay() {
        showDebugOverlay = !showDebugOverlay;
    }

    // Get debug info string for overlay
    juce::String getDebugInfo() const {
        juce::String info;
        info << "=== LayoutConfig ===\n";
        info << "Timeline Total: " << getTimelineHeight() << "px\n";
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

}  // namespace magica
