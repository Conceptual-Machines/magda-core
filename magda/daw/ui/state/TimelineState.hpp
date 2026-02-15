#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <set>
#include <vector>

#include "../layout/LayoutConfig.hpp"

namespace magda {

// Time display mode for timeline
enum class TimeDisplayMode {
    Seconds,   // Display as 0.0s, 1.0s, 2.0s, etc.
    BarsBeats  // Display as 1.1.1, 1.2.1, 2.1.1, etc. (bar.beat.subdivision)
};

// Grid quantize setting (numerator/denominator + auto toggle)
struct GridQuantize {
    bool autoGrid = true;  // When true, use smart grid based on zoom level
    int numerator = 1;     // e.g. 1, 2, 3
    int denominator = 4;   // Must be power of 2: 1, 2, 4, 8, 16, 32

    // Display-only: effective grid when in auto mode (set by MIDI editors)
    int autoEffectiveNumerator = 1;
    int autoEffectiveDenominator = 16;

    // Returns beat fraction: numerator/denominator relative to whole note (4 beats)
    // e.g. 1/4 = 1 beat, 1/8 = 0.5 beats, 3/8 = 1.5 beats
    double toBeatFraction() const {
        return (4.0 * numerator) / denominator;
    }
};

/**
 * Centralized grid utilities. Beat subdivisions and bar multiples are
 * computed as powers of 2, so the grid scales to any zoom level.
 *
 * Beat fractions: 2^n for n = minBeatPow..maxBeatPow (fractions of a beat).
 *   e.g. 2^-6 = 1/64 beat = 1/256 note, 2^0 = 1 beat = 1/4 note, 2^1 = 2 beats = 1/2 note.
 *
 * Display: denominator = 4 / beatFraction (whole-note relative).
 *   e.g. beatFraction=0.25 -> 1/16, beatFraction=1.0 -> 1/4, beatFraction=2.0 -> 1/2.
 */
struct GridConstants {
    // Range of beat fraction powers: 2^minBeatPow .. 2^maxBeatPow
    static constexpr int minBeatPow = -6;  // 2^-6 = 1/64 beat = 1/256 note
    static constexpr int maxBeatPow = 1;   // 2^1  = 2 beats  = 1/2 note

    // Max bar multiple power: 2^0 .. 2^maxBarPow bars
    static constexpr int maxBarPow = 5;  // 2^5 = 32 bars

    // Get beat fraction for a given power
    static constexpr double beatFraction(int pow) {
        // Manual constexpr pow2: positive shifts up, negative shifts down
        if (pow >= 0) {
            return static_cast<double>(1 << pow);
        } else {
            return 1.0 / static_cast<double>(1 << (-pow));
        }
    }

    // Get display denominator for a beat fraction (whole-note relative: 4/fraction)
    static constexpr int denominator(int pow) {
        // denom = 4 / beatFraction = 4 * 2^(-pow) = 2^(2-pow)
        int shift = 2 - pow;
        return (shift >= 0) ? (1 << shift) : 1;
    }

    /**
     * Find the first beat subdivision where pixelSpacing >= minPixels.
     * Returns the beat fraction, or -1 if none found (caller should try bar multiples).
     */
    static double findBeatSubdivision(double zoom, int minPixels) {
        for (int p = minBeatPow; p <= maxBeatPow; p++) {
            double frac = beatFraction(p);
            if (static_cast<int>(frac * zoom) >= minPixels) {
                return frac;
            }
        }
        return -1.0;
    }

    /**
     * Find the first bar multiple where pixelSpacing >= minPixels.
     * Returns the bar multiple count (1, 2, 4, ...), or the max as fallback.
     */
    static int findBarMultiple(double zoom, int timeSigNumerator, int minPixels) {
        for (int p = 0; p <= maxBarPow; p++) {
            int mult = 1 << p;
            double pixelSpacing = zoom * timeSigNumerator * mult;
            if (static_cast<int>(pixelSpacing) >= minPixels) {
                return mult;
            }
        }
        return 1 << maxBarPow;
    }

    // ===== Grid alignment and classification utilities =====

    /** Check if grid interval evenly divides bars (or spans multiple bars). */
    static bool gridAlignsWithBars(double intervalBeats, double barLengthBeats) {
        double barMod = std::fmod(barLengthBeats, intervalBeats);
        return intervalBeats >= barLengthBeats || barMod < 0.001 ||
               barMod > (intervalBeats - 0.001);
    }

    /** Check if grid interval evenly divides beats (or spans multiple beats). */
    static bool gridAlignsWithBeats(double intervalBeats) {
        double beatMod = std::fmod(1.0, intervalBeats);
        return intervalBeats >= 1.0 || beatMod < 0.001 || beatMod > (intervalBeats - 0.001);
    }

    /** Result of classifying a beat position within the bar/beat hierarchy. */
    struct BeatClassification {
        bool isBar;
        bool isBeat;
    };

    /** Classify a beat position as bar start, beat start, or subdivision. */
    static BeatClassification classifyBeatPosition(double beatPosition, double barLengthBeats) {
        double barRemainder = std::fmod(beatPosition, barLengthBeats);
        bool isBar = barRemainder < 0.001 || barRemainder > (barLengthBeats - 0.001);
        double beatRemainder = std::fmod(beatPosition, 1.0);
        bool isBeat = isBar || beatRemainder < 0.001 || beatRemainder > 0.999;
        return {isBar, isBeat};
    }

    /**
     * Compute grid interval in beats, respecting auto/manual mode.
     * Returns the interval in beats (e.g. 0.5 for 1/8, 4.0 for 1 bar in 4/4).
     */
    static double computeGridInterval(const GridQuantize& gridQuantize, double zoom,
                                      int timeSigNumerator, int minPixelSpacing) {
        if (!gridQuantize.autoGrid) {
            return gridQuantize.toBeatFraction();
        }
        double frac = findBeatSubdivision(zoom, minPixelSpacing);
        if (frac > 0) {
            return frac;
        }
        int mult = findBarMultiple(zoom, timeSigNumerator, minPixelSpacing);
        return static_cast<double>(timeSigNumerator) * mult;
    }
};

/**
 * @brief Zoom state for the timeline
 */
struct ZoomState {
    double horizontalZoom = 10.0;  // Pixels per beat
    double verticalZoom = 1.0;     // Track height multiplier
    int scrollX = 0;               // Horizontal scroll position in pixels
    int scrollY = 0;               // Vertical scroll position in pixels
    int viewportWidth = 800;       // Current viewport width
    int viewportHeight = 600;      // Current viewport height
};

/**
 * @brief Playhead state (Bitwig-style dual playhead)
 *
 * - editPosition: Where the triangle sits (stationary during playback)
 * - playbackPosition: Where playback currently is (moving cursor)
 *
 * When playback stops, playbackPosition resets to editPosition.
 */
struct PlayheadState {
    double editPosition = 0.0;       // Triangle position in seconds (derived from beats)
    double editPositionBeats = 0.0;  // Triangle position in beats (authoritative)
    double playbackPosition = 0.0;   // Moving cursor position
    bool isPlaying = false;          // Is transport playing
    bool isRecording = false;        // Is transport recording

    // Get the "current" position (playback when playing, edit otherwise)
    double getCurrentPosition() const {
        return isPlaying ? playbackPosition : editPosition;
    }

    // For backwards compatibility - returns the effective playhead position
    double getPosition() const {
        return getCurrentPosition();
    }
};

/**
 * @brief Time selection state (temporary range highlight)
 *
 * Supports per-track selection via trackIndices set.
 * Empty trackIndices = all tracks selected (backward compatible).
 */
struct TimeSelection {
    double startTime = -1.0;      // Time in seconds (derived from beats)
    double endTime = -1.0;        // Time in seconds (derived from beats)
    double startBeats = -1.0;     // Position in beats (authoritative)
    double endBeats = -1.0;       // Position in beats (authoritative)
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
        startBeats = -1.0;
        endBeats = -1.0;
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
    double startTime = -1.0;   // Time in seconds (derived from beats)
    double endTime = -1.0;     // Time in seconds (derived from beats)
    double startBeats = -1.0;  // Position in beats (authoritative)
    double endBeats = -1.0;    // Position in beats (authoritative)
    bool enabled = false;

    bool isValid() const {
        return startTime >= 0 && endTime > startTime;
    }
    void clear() {
        startTime = -1.0;
        endTime = -1.0;
        startBeats = -1.0;
        endBeats = -1.0;
        enabled = false;
    }
    double getDuration() const {
        return isValid() ? (endTime - startTime) : 0.0;
    }
};

/**
 * @brief Punch in/out region state (independent markers for punch recording)
 */
struct PunchRegion {
    double startTime = -1.0;   // Time in seconds (derived from beats)
    double endTime = -1.0;     // Time in seconds (derived from beats)
    double startBeats = -1.0;  // Position in beats (authoritative)
    double endBeats = -1.0;    // Position in beats (authoritative)
    bool punchInEnabled = false;
    bool punchOutEnabled = false;

    bool isValid() const {
        return startTime >= 0 && endTime > startTime;
    }
    bool isEnabled() const {
        return punchInEnabled || punchOutEnabled;
    }
    void clear() {
        startTime = -1.0;
        endTime = -1.0;
        startBeats = -1.0;
        endBeats = -1.0;
        punchInEnabled = false;
        punchOutEnabled = false;
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
    GridQuantize gridQuantize;
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

    // Edit cursor - separate from playhead, used for split/edit operations
    // Set by clicking in lower track zone, independent of playback position
    double editCursorPosition = -1.0;  // -1 means not set/hidden

    // Sub-states
    ZoomState zoom;
    PlayheadState playhead;
    TimeSelection selection;
    LoopRegion loop;
    PunchRegion punch;
    TempoState tempo;
    DisplayConfig display;

    // Arrangement sections
    std::vector<ArrangementSection> sections;
    int selectedSectionIndex = -1;

    // Layout constant (shared across components)
    static constexpr int LEFT_PADDING = LayoutConfig::TIMELINE_LEFT_PADDING;

    // ===== Beat/time conversion helpers =====

    /** Convert seconds to beats using current tempo */
    double secondsToBeats(double seconds) const {
        return seconds * tempo.bpm / 60.0;
    }

    /** Convert beats to seconds using current tempo */
    double beatsToSeconds(double beats) const {
        return beats * 60.0 / tempo.bpm;
    }

    // ===== Coordinate conversion helpers =====
    // horizontalZoom is in pixels per beat.
    // All time↔pixel conversions go through beats.

    /**
     * Convert a pixel position to time (accounting for scroll and padding)
     */
    double pixelToTime(int pixel) const {
        if (zoom.horizontalZoom > 0) {
            double beats = (pixel + zoom.scrollX - LEFT_PADDING) / zoom.horizontalZoom;
            return beatsToSeconds(beats);
        }
        return 0.0;
    }

    /**
     * Convert a pixel position to time (local to component, no scroll adjustment)
     */
    double pixelToTimeLocal(int pixel) const {
        if (zoom.horizontalZoom > 0) {
            double beats = (pixel - LEFT_PADDING) / zoom.horizontalZoom;
            return beatsToSeconds(beats);
        }
        return 0.0;
    }

    /**
     * Convert time to pixel position (accounting for scroll and padding)
     */
    int timeToPixel(double time) const {
        double beats = secondsToBeats(time);
        return static_cast<int>(beats * zoom.horizontalZoom) + LEFT_PADDING - zoom.scrollX;
    }

    /**
     * Convert time to pixel position (local to component, no scroll adjustment)
     */
    int timeToPixelLocal(double time) const {
        double beats = secondsToBeats(time);
        return static_cast<int>(beats * zoom.horizontalZoom) + LEFT_PADDING;
    }

    /**
     * Convert a time duration to pixels (zoom-dependent, no padding)
     */
    int timeDurationToPixels(double duration) const {
        double beats = secondsToBeats(duration);
        return static_cast<int>(beats * zoom.horizontalZoom);
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
        // If grid override is active, return the fixed interval
        if (!display.gridQuantize.autoGrid) {
            double beatFraction = display.gridQuantize.toBeatFraction();
            return tempo.getSecondsPerBeat() * beatFraction;
        }

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
            // BarsBeats: zoom is ppb, find first power-of-2 beat fraction that fits
            double frac = GridConstants::findBeatSubdivision(zoom.horizontalZoom, minPixelSpacing);
            if (frac > 0) {
                return tempo.getSecondsPerBeat() * frac;
            }
            // Fall back to bar multiples
            int mult = GridConstants::findBarMultiple(
                zoom.horizontalZoom, tempo.timeSignatureNumerator, minPixelSpacing);
            return tempo.getSecondsPerBar() * mult;
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
        double beats = secondsToBeats(timelineLength);
        int baseWidth = static_cast<int>(beats * zoom.horizontalZoom);
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
     * Calculate minimum zoom level (ppb) to fit timeline in viewport
     */
    double getMinZoom() const {
        if (timelineLength > 0 && zoom.viewportWidth > 0) {
            double availableWidth = zoom.viewportWidth - 50.0;
            double beats = secondsToBeats(timelineLength);
            if (beats > 0) {
                // Allow zooming out to 1/4 of the fit-to-viewport level
                return (availableWidth / beats) * 0.25;
            }
        }
        return 0.01;
    }
};

}  // namespace magda
