#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>
#include <vector>

namespace magica {

// Time display mode for timeline
enum class TimeDisplayMode {
    Seconds,   // Display as 0.0s, 1.0s, 2.0s, etc.
    BarsBeats  // Display as 1.1.1, 1.2.1, 2.1.1, etc. (bar.beat.subdivision)
};

/**
 * @brief Zoom state for the timeline
 */
struct ZoomState {
    double horizontalZoom = 20.0;  // Pixels per second
    double verticalZoom = 1.0;     // Track height multiplier
    int scrollX = 0;               // Horizontal scroll position in pixels
    int scrollY = 0;               // Vertical scroll position in pixels
    int viewportWidth = 800;       // Current viewport width
    int viewportHeight = 600;      // Current viewport height
};

/**
 * @brief Playhead state
 */
struct PlayheadState {
    double position = 0.0;     // Current position in seconds
    bool isPlaying = false;    // Is transport playing
    bool isRecording = false;  // Is transport recording
};

/**
 * @brief Time selection state (temporary range highlight)
 *
 * Supports per-track selection via trackIndices set.
 * Empty trackIndices = all tracks selected (backward compatible).
 */
struct TimeSelection {
    double startTime = -1.0;
    double endTime = -1.0;
    std::set<int> trackIndices;   // Empty = all tracks
    bool visuallyHidden = false;  // When true, selection is hidden visually but data remains

    bool isActive() const {
        return startTime >= 0 && endTime > startTime;
    }
    bool isVisuallyActive() const {
        return isActive() && !visuallyHidden;
    }
    bool isAllTracks() const {
        return trackIndices.empty();
    }
    bool includesTrack(int trackIndex) const {
        return trackIndices.empty() || trackIndices.count(trackIndex) > 0;
    }
    void clear() {
        startTime = -1.0;
        endTime = -1.0;
        trackIndices.clear();
        visuallyHidden = false;
    }
    void hideVisually() {
        visuallyHidden = true;
    }
    void showVisually() {
        visuallyHidden = false;
    }
    double getDuration() const {
        return isActive() ? (endTime - startTime) : 0.0;
    }
};

/**
 * @brief Loop region state (persistent loop markers)
 */
struct LoopRegion {
    double startTime = -1.0;
    double endTime = -1.0;
    bool enabled = false;

    bool isValid() const {
        return startTime >= 0 && endTime > startTime;
    }
    void clear() {
        startTime = -1.0;
        endTime = -1.0;
        enabled = false;
    }
    double getDuration() const {
        return isValid() ? (endTime - startTime) : 0.0;
    }
};

/**
 * @brief Tempo and time signature state
 */
struct TempoState {
    double bpm = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;

    // Helper methods
    double getSecondsPerBeat() const {
        return 60.0 / bpm;
    }
    double getSecondsPerBar() const {
        return getSecondsPerBeat() * timeSignatureNumerator;
    }
    double timeToBars(double timeInSeconds) const {
        double beatsPerSecond = bpm / 60.0;
        double totalBeats = timeInSeconds * beatsPerSecond;
        return totalBeats / timeSignatureNumerator;
    }
    double barsToTime(double bars) const {
        double totalBeats = bars * timeSignatureNumerator;
        return totalBeats * getSecondsPerBeat();
    }
};

/**
 * @brief Display configuration
 */
struct DisplayConfig {
    TimeDisplayMode timeDisplayMode = TimeDisplayMode::BarsBeats;
    bool snapEnabled = true;
    bool arrangementLocked = true;
};

/**
 * @brief Arrangement section
 */
struct ArrangementSection {
    double startTime;
    double endTime;
    juce::String name;
    juce::Colour colour;

    ArrangementSection(double start = 0.0, double end = 0.0,
                       const juce::String& sectionName = "Section",
                       juce::Colour sectionColour = juce::Colours::blue)
        : startTime(start), endTime(end), name(sectionName), colour(sectionColour) {}

    double getDuration() const {
        return endTime - startTime;
    }
};

/**
 * @brief Complete timeline state - the single source of truth
 *
 * This struct holds ALL timeline-related state. Components read from this
 * and dispatch events to modify it via the TimelineController.
 */
struct TimelineState {
    // Core timeline properties
    double timelineLength = 300.0;  // Total length in seconds

    // Sub-states
    ZoomState zoom;
    PlayheadState playhead;
    TimeSelection selection;
    LoopRegion loop;
    TempoState tempo;
    DisplayConfig display;

    // Arrangement sections
    std::vector<ArrangementSection> sections;
    int selectedSectionIndex = -1;

    // Layout constant (shared across components)
    static constexpr int LEFT_PADDING = 18;

    // ===== Coordinate conversion helpers =====

    /**
     * Convert a pixel position to time (accounting for scroll and padding)
     */
    double pixelToTime(int pixel) const {
        if (zoom.horizontalZoom > 0) {
            return (pixel + zoom.scrollX - LEFT_PADDING) / zoom.horizontalZoom;
        }
        return 0.0;
    }

    /**
     * Convert a pixel position to time (local to component, no scroll adjustment)
     */
    double pixelToTimeLocal(int pixel) const {
        if (zoom.horizontalZoom > 0) {
            return (pixel - LEFT_PADDING) / zoom.horizontalZoom;
        }
        return 0.0;
    }

    /**
     * Convert time to pixel position (accounting for scroll and padding)
     */
    int timeToPixel(double time) const {
        return static_cast<int>(time * zoom.horizontalZoom) + LEFT_PADDING - zoom.scrollX;
    }

    /**
     * Convert time to pixel position (local to component, no scroll adjustment)
     */
    int timeToPixelLocal(double time) const {
        return static_cast<int>(time * zoom.horizontalZoom) + LEFT_PADDING;
    }

    /**
     * Convert a time duration to pixels (zoom-dependent, no padding)
     */
    int timeDurationToPixels(double duration) const {
        return static_cast<int>(duration * zoom.horizontalZoom);
    }

    /**
     * Snap a time value to the current grid
     */
    double snapTimeToGrid(double time) const {
        if (!display.snapEnabled) {
            return time;
        }

        double interval = getSnapInterval();
        if (interval <= 0) {
            return time;
        }

        return std::round(time / interval) * interval;
    }

    /**
     * Get the current snap interval based on zoom level and display mode
     */
    double getSnapInterval() const {
        const int minPixelSpacing = 50;  // From LayoutConfig

        if (display.timeDisplayMode == TimeDisplayMode::Seconds) {
            const double intervals[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1,  0.2, 0.25,
                                        0.5,   1.0,   2.0,   5.0,  10.0, 15.0, 30.0, 60.0};
            for (double interval : intervals) {
                if (timeDurationToPixels(interval) >= minPixelSpacing) {
                    return interval;
                }
            }
            return 1.0;
        } else {
            double secondsPerBeat = tempo.getSecondsPerBeat();
            const double beatFractions[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0};

            for (double fraction : beatFractions) {
                double intervalSeconds = secondsPerBeat * fraction;
                if (timeDurationToPixels(intervalSeconds) >= minPixelSpacing) {
                    return intervalSeconds;
                }
            }

            return tempo.getSecondsPerBar();
        }
    }

    /**
     * Format a time position for display
     */
    juce::String formatTimePosition(double timeInSeconds) const {
        if (display.timeDisplayMode == TimeDisplayMode::Seconds) {
            if (timeInSeconds < 10.0) {
                return juce::String(timeInSeconds, 1) + "s";
            } else if (timeInSeconds < 60.0) {
                return juce::String(timeInSeconds, 0) + "s";
            } else {
                int minutes = static_cast<int>(timeInSeconds) / 60;
                int seconds = static_cast<int>(timeInSeconds) % 60;
                return juce::String(minutes) + ":" + juce::String(seconds).paddedLeft('0', 2);
            }
        } else {
            double beatsPerSecond = tempo.bpm / 60.0;
            double totalBeats = timeInSeconds * beatsPerSecond;

            int bar = static_cast<int>(totalBeats / tempo.timeSignatureNumerator) + 1;
            int beatInBar =
                static_cast<int>(std::fmod(totalBeats, tempo.timeSignatureNumerator)) + 1;
            double beatFraction = std::fmod(totalBeats, 1.0);
            int subdivision = static_cast<int>(beatFraction * 4) + 1;

            return juce::String(bar) + "." + juce::String(beatInBar) + "." +
                   juce::String(subdivision);
        }
    }

    /**
     * Calculate content width based on zoom and timeline length
     */
    int getContentWidth() const {
        int baseWidth = static_cast<int>(timelineLength * zoom.horizontalZoom);
        int minWidth = zoom.viewportWidth + (zoom.viewportWidth / 2);
        return juce::jmax(baseWidth, minWidth);
    }

    /**
     * Calculate maximum scroll position
     */
    int getMaxScrollX() const {
        return juce::jmax(0, getContentWidth() - zoom.viewportWidth);
    }

    /**
     * Calculate minimum zoom level to fit timeline in viewport
     */
    double getMinZoom() const {
        if (timelineLength > 0 && zoom.viewportWidth > 0) {
            double availableWidth = zoom.viewportWidth - 50.0;
            return availableWidth / timelineLength;
        }
        return 0.1;
    }
};

}  // namespace magica
