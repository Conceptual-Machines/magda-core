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
    int chordRowHeight = 0;        // Chord row disabled (now in piano roll only)
    int markerLaneHeight = 24;     // Named timeline markers above the existing ruler
    int arrangementBarHeight = 0;  // Arrangement sections disabled in the compact ruler
    int timeRulerHeight = 60;      // Full compact ruler height

    // Time ruler details
    int rulerMajorTickHeight = 14;              // Shortened to avoid overlap with loop markers
    int rulerMinorTickHeight = 6;               // Shortened to avoid overlap with loop markers
    static constexpr int loopStripHeight = 12;  // Loop row (loop region strip)
    int secondsRowHeight = 11;                  // Seconds row (when shown)
    int playheadRowHeight = 12;                 // Bottom row: just tall enough for the triangle
    int rulerLabelFontSize = 11;
    int rulerLabelTopMargin = 10;  // Space between separator line and time labels

    // Grid/tick spacing - shared between timeline ruler and track content grid
    int minGridPixelSpacing = 50;  // Minimum pixels between grid lines/ticks

    // Debug mode
    bool showDebugOverlay = false;

    // Computed total timeline height
    int getTimelineHeight() const {
        return markerLaneHeight + getTimelineBodyHeight();
    }

    int getTimelineBodyHeight() const {
        return chordRowHeight + arrangementBarHeight + timeRulerHeight;
    }

    // Track layout
    int defaultTrackHeight = 80;
    int minTrackHeight = 40;
    int maxTrackHeight = 200;

    // Track headers
    int defaultTrackHeaderWidth = 200;
    int minTrackHeaderWidth = 200;
    int maxTrackHeaderWidth = 300;

    // Spacing and padding
    //
    // These are density-scaled: the base (normal-density) values live in the
    // kBase* constants below, and applyDensityScale() recomputes the live
    // fields as base * densityScale. Density affects spacing/padding only, not
    // fonts or widget/track/panel sizes (those stay at the user's chosen size).
    int headerContentPadding = 8;
    int componentSpacing = 4;
    int panelPadding = 8;

    // Base (normal-density) spacing values. applyDensityScale() derives the
    // live fields above from these, so re-applying a scale never compounds.
    static constexpr int kBaseHeaderContentPadding = 8;
    static constexpr int kBaseComponentSpacing = 4;
    static constexpr int kBasePanelPadding = 8;
    static constexpr int kBaseRulerLabelTopMargin = 10;

    // Active UI spacing-density multiplier (1.0 = normal). Read by the
    // densityScaled() free helper so inline spacing literals can scale too.
    float densityScale = 1.0f;

    // Recompute all density-scaled spacing tokens from their base values.
    // Idempotent: always derived from kBase*, so calling repeatedly is safe.
    void applyDensityScale(float scale) {
        densityScale = scale;
        headerContentPadding = juce::roundToInt(kBaseHeaderContentPadding * scale);
        componentSpacing = juce::roundToInt(kBaseComponentSpacing * scale);
        panelPadding = juce::roundToInt(kBasePanelPadding * scale);
        rulerLabelTopMargin = juce::roundToInt(kBaseRulerLabelTopMargin * scale);
    }

    // Timeline content left padding - shared across timeline, track content, automation lanes
    static constexpr int TIMELINE_LEFT_PADDING = 8;

    // Left padding for the MIDI/drum grid bodies + their ruler, sized so the
    // playhead triangle (6px half-width) at bar 1 clears the left column. Single
    // source for both the piano-roll and drum-grid editors so they can't drift.
    static constexpr int MIDI_GRID_LEFT_PADDING = 8;

    // Zoom controls
    int zoomButtonSize = 24;
    int zoomSliderMinWidth = 60;

    // Main window panels
    int defaultTransportHeight = 48;
    int minTransportHeight = 40;
    int maxTransportHeight = 55;

    int footerHeight = 40;

    int defaultLeftPanelWidth = 300;
    int defaultRightPanelWidth = 300;
    int minPanelWidth = 200;
    int collapsedPanelSize = 24;
    int panelCollapseThreshold = 50;

    int defaultBottomPanelHeight = 360;
    int minBottomPanelHeight = 200;

    // Max panel size constraints (fraction of window dimension)
    float maxLeftPanelRatio = 0.4f;    // Max 40% of window width
    float maxRightPanelRatio = 0.4f;   // Max 40% of window width
    float maxBottomPanelRatio = 0.6f;  // Max 60% of window height

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
        info << "  markerLaneHeight: " << markerLaneHeight << "\n";
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

// Scales a raw spacing/padding literal by the active UI density. Use this when
// migrating inline spacing (e.g. `area.reduced(densityScaled(4))`) so it tracks
// the compact/normal/spacious preset instead of staying pinned at one density.
inline int densityScaled(int basePx) {
    return juce::roundToInt(basePx * LayoutConfig::getInstance().densityScale);
}

}  // namespace magda
