#pragma once

#include <algorithm>
#include <cmath>

namespace magica {

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

}  // namespace TimelineUtils

}  // namespace magica
