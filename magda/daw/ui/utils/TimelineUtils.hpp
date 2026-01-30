#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace magda {

/**
 * @brief Utility functions for timeline time/pixel conversions
 *
 * These are pure functions that can be used by any component.
 * Each component provides its own zoom and padding values.
 */
namespace TimelineUtils {

/**
 * Convert a time value to pixel position
 * @param time Time in seconds
 * @param pixelsPerSecond Zoom level (pixels per second)
 * @param leftPadding Left padding offset in pixels
 * @return Pixel position
 */
inline int timeToPixel(double time, double pixelsPerSecond, int leftPadding = 0) {
    return static_cast<int>(time * pixelsPerSecond) + leftPadding;
}

/**
 * Convert a pixel position to time value
 * @param pixel Pixel position
 * @param pixelsPerSecond Zoom level (pixels per second)
 * @param leftPadding Left padding offset in pixels
 * @return Time in seconds
 */
inline double pixelToTime(int pixel, double pixelsPerSecond, int leftPadding = 0) {
    if (pixelsPerSecond <= 0) {
        return 0.0;
    }
    return static_cast<double>(pixel - leftPadding) / pixelsPerSecond;
}

/**
 * Convert a time duration to pixel width (no padding involved)
 * @param duration Duration in seconds
 * @param pixelsPerSecond Zoom level (pixels per second)
 * @return Width in pixels
 */
inline int durationToPixels(double duration, double pixelsPerSecond) {
    return static_cast<int>(duration * pixelsPerSecond);
}

/**
 * Convert a pixel width to time duration
 * @param pixels Width in pixels
 * @param pixelsPerSecond Zoom level (pixels per second)
 * @return Duration in seconds
 */
inline double pixelsToDuration(int pixels, double pixelsPerSecond) {
    if (pixelsPerSecond <= 0) {
        return 0.0;
    }
    return static_cast<double>(pixels) / pixelsPerSecond;
}

/**
 * Snap a time value to a grid interval
 * @param time Time in seconds
 * @param gridInterval Grid interval in seconds
 * @return Snapped time value
 */
inline double snapToGrid(double time, double gridInterval) {
    if (gridInterval <= 0) {
        return time;
    }
    return std::round(time / gridInterval) * gridInterval;
}

/**
 * Check if a time value is within magnetic snap range of a grid line
 * @param time Time in seconds
 * @param gridInterval Grid interval in seconds
 * @param pixelsPerSecond Zoom level
 * @param snapThresholdPixels Magnetic snap threshold in pixels
 * @return True if within snap range
 */
inline bool isWithinSnapRange(double time, double gridInterval, double pixelsPerSecond,
                              int snapThresholdPixels) {
    if (gridInterval <= 0) {
        return false;
    }
    double snappedTime = snapToGrid(time, gridInterval);
    double deltaPixels = std::abs((snappedTime - time) * pixelsPerSecond);
    return deltaPixels <= snapThresholdPixels;
}

/**
 * Get the snapped time if within range, otherwise return original
 * @param time Time in seconds
 * @param gridInterval Grid interval in seconds
 * @param pixelsPerSecond Zoom level
 * @param snapThresholdPixels Magnetic snap threshold in pixels
 * @return Snapped time if within range, otherwise original time
 */
inline double magneticSnap(double time, double gridInterval, double pixelsPerSecond,
                           int snapThresholdPixels) {
    if (isWithinSnapRange(time, gridInterval, pixelsPerSecond, snapThresholdPixels)) {
        return snapToGrid(time, gridInterval);
    }
    return time;
}

/**
 * Convert beats to seconds
 * @param beats Number of beats
 * @param bpm Tempo in beats per minute
 * @return Time in seconds
 */
inline double beatsToSeconds(double beats, double bpm) {
    if (bpm <= 0) {
        return 0.0;
    }
    return beats * 60.0 / bpm;
}

/**
 * Convert seconds to beats
 * @param seconds Time in seconds
 * @param bpm Tempo in beats per minute
 * @return Number of beats
 */
inline double secondsToBeats(double seconds, double bpm) {
    if (bpm <= 0) {
        return 0.0;
    }
    return seconds * bpm / 60.0;
}

/**
 * Get bar number from time (1-indexed for display)
 * @param time Time in seconds
 * @param bpm Tempo in beats per minute
 * @param beatsPerBar Time signature numerator
 * @return Bar number (1-indexed)
 */
inline int getBarNumber(double time, double bpm, int beatsPerBar) {
    double beats = secondsToBeats(time, bpm);
    return static_cast<int>(beats / beatsPerBar) + 1;
}

/**
 * Get beat within bar (1-indexed for display)
 * @param time Time in seconds
 * @param bpm Tempo in beats per minute
 * @param beatsPerBar Time signature numerator
 * @return Beat number within bar (1-indexed)
 */
inline int getBeatInBar(double time, double bpm, int beatsPerBar) {
    double beats = secondsToBeats(time, bpm);
    return static_cast<int>(std::fmod(beats, beatsPerBar)) + 1;
}

/**
 * Get time at start of a bar
 * @param barNumber Bar number (1-indexed)
 * @param bpm Tempo in beats per minute
 * @param beatsPerBar Time signature numerator
 * @return Time in seconds
 */
inline double getBarStartTime(int barNumber, double bpm, int beatsPerBar) {
    double beats = (barNumber - 1) * beatsPerBar;
    return beatsToSeconds(beats, bpm);
}

/**
 * Get tick within beat (0-based, for sub-beat precision)
 * Uses 480 ticks per beat (standard MIDI resolution)
 * @param time Time in seconds
 * @param bpm Tempo in beats per minute
 * @return Tick number within beat (0-479)
 */
inline int getTickInBeat(double time, double bpm) {
    constexpr int TICKS_PER_BEAT = 480;
    double beats = secondsToBeats(time, bpm);
    double fractionalBeat = std::fmod(beats, 1.0);
    return static_cast<int>(fractionalBeat * TICKS_PER_BEAT);
}

/**
 * Format time as bars.beats.ticks string (e.g., "1.1.000", "4.3.240")
 * @param time Time in seconds
 * @param bpm Tempo in beats per minute
 * @param beatsPerBar Time signature numerator
 * @return Formatted string "bar.beat.ticks"
 */
inline std::string formatTimeAsBarsBeats(double time, double bpm, int beatsPerBar) {
    int bar = getBarNumber(time, bpm, beatsPerBar);
    int beat = getBeatInBar(time, bpm, beatsPerBar);
    int ticks = getTickInBeat(time, bpm);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d.%d.%03d", bar, beat, ticks);
    return std::string(buffer);
}

/**
 * Format duration as bars:beats string (e.g., "2 bars", "1 bar 2 beats")
 * @param duration Duration in seconds
 * @param bpm Tempo in beats per minute
 * @param beatsPerBar Time signature numerator
 * @return Formatted string describing duration
 */
inline std::string formatDurationAsBarsBeats(double duration, double bpm, int beatsPerBar) {
    double totalBeats = secondsToBeats(duration, bpm);
    int wholeBars = static_cast<int>(totalBeats / beatsPerBar);
    double remainingBeats = std::fmod(totalBeats, beatsPerBar);
    int wholeBeats = static_cast<int>(remainingBeats);

    char buffer[64];
    if (wholeBars > 0 && wholeBeats > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d bar%s %d beat%s", wholeBars,
                      wholeBars == 1 ? "" : "s", wholeBeats, wholeBeats == 1 ? "" : "s");
    } else if (wholeBars > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d bar%s", wholeBars, wholeBars == 1 ? "" : "s");
    } else if (totalBeats >= 1.0) {
        std::snprintf(buffer, sizeof(buffer), "%d beat%s", static_cast<int>(totalBeats),
                      static_cast<int>(totalBeats) == 1 ? "" : "s");
    } else {
        // Sub-beat duration - show as fraction
        std::snprintf(buffer, sizeof(buffer), "%.2f beats", totalBeats);
    }
    return std::string(buffer);
}

/**
 * Format duration as compact bars.beats string (e.g., "2.0", "1.2")
 * @param duration Duration in seconds
 * @param bpm Tempo in beats per minute
 * @param beatsPerBar Time signature numerator
 * @return Formatted string "bars.beats"
 */
inline std::string formatDurationCompact(double duration, double bpm, int beatsPerBar) {
    double totalBeats = secondsToBeats(duration, bpm);
    int wholeBars = static_cast<int>(totalBeats / beatsPerBar);
    double remainingBeats = std::fmod(totalBeats, beatsPerBar);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d.%.1f", wholeBars, remainingBeats);
    return std::string(buffer);
}

/**
 * Format a beat count as bars.beats.ticks duration string (e.g., "1.0.000", "2.3.240")
 * Unlike formatTimeAsBarsBeats, this is 0-indexed for duration display.
 * @param totalBeats Duration in beats
 * @param beatsPerBar Time signature numerator
 * @return Formatted string "bars.beats.ticks"
 */
inline std::string formatBeatsAsBarsBeats(double totalBeats, int beatsPerBar) {
    constexpr int TICKS_PER_BEAT = 480;
    int wholeBars = static_cast<int>(totalBeats / beatsPerBar);
    double remaining = std::fmod(totalBeats, beatsPerBar);
    int wholeBeats = static_cast<int>(remaining);
    int ticks = static_cast<int>((remaining - wholeBeats) * TICKS_PER_BEAT);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d.%d.%03d", wholeBars, wholeBeats, ticks);
    return std::string(buffer);
}

}  // namespace TimelineUtils

}  // namespace magda
