#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace magda::velocity_lane {

/// Convert a beat position to pixel x coordinate.
inline int beatToPixel(double beat, double pixelsPerBeat, int leftPadding, int scrollOffsetX) {
    return static_cast<int>(beat * pixelsPerBeat) + leftPadding - scrollOffsetX;
}

/// Convert a pixel x coordinate to beat position.
inline double pixelToBeat(int x, double pixelsPerBeat, int leftPadding, int scrollOffsetX) {
    return (x + scrollOffsetX - leftPadding) / pixelsPerBeat;
}

/// Convert a MIDI velocity (0–127) to a y pixel coordinate.
/// Velocity 127 maps to the top, 0 to the bottom, with a small margin.
inline int velocityToY(int velocity, int componentHeight, int margin = 2) {
    int usableHeight = componentHeight - (margin * 2);
    return margin + usableHeight - (velocity * usableHeight / 127);
}

/// Convert a y pixel coordinate to MIDI velocity (0–127).
inline int yToVelocity(int y, int componentHeight, int margin = 2) {
    int usableHeight = componentHeight - (margin * 2);
    int velocity = 127 - ((y - margin) * 127 / usableHeight);
    return std::clamp(velocity, 0, 127);
}

/// Interpolate velocity along a ramp/curve.
/// @param t            Normalized position (0.0 = first note, 1.0 = last note)
/// @param startVel     Velocity at t=0
/// @param endVel       Velocity at t=1
/// @param curveAmount  Curve bend (-1.0 to 1.0, 0 = linear)
inline int interpolateVelocity(float t, int startVel, int endVel, float curveAmount = 0.0f) {
    if (std::abs(curveAmount) < 0.001f) {
        // Linear interpolation
        return std::clamp(startVel + static_cast<int>(t * (endVel - startVel)), 0, 127);
    }
    // Quadratic bezier with control point
    float controlVel = (startVel + endVel) * 0.5f + curveAmount * 127.0f;
    controlVel = std::clamp(controlVel, 0.0f, 127.0f);
    float v = (1 - t) * (1 - t) * startVel + 2 * (1 - t) * t * controlVel + t * t * endVel;
    return std::clamp(static_cast<int>(std::round(v)), 0, 127);
}

/// Compute ramp velocities for a set of notes sorted by beat position.
/// @param sortedBeatPositions  Beat position for each note (must be sorted ascending)
/// @param startVel             Velocity at the first note
/// @param endVel               Velocity at the last note
/// @param curveAmount          Curve bend (-1.0 to 1.0, 0 = linear)
/// @return Vector of interpolated velocities, one per input position.
inline std::vector<int> computeRampVelocities(const std::vector<double>& sortedBeatPositions,
                                              int startVel, int endVel, float curveAmount = 0.0f) {
    std::vector<int> result;
    if (sortedBeatPositions.size() < 2) {
        return result;
    }

    double firstBeat = sortedBeatPositions.front();
    double lastBeat = sortedBeatPositions.back();
    double range = lastBeat - firstBeat;

    result.reserve(sortedBeatPositions.size());
    for (double beat : sortedBeatPositions) {
        float t = (range > 0.0) ? static_cast<float>((beat - firstBeat) / range) : 0.0f;
        result.push_back(interpolateVelocity(t, startVel, endVel, curveAmount));
    }

    return result;
}

}  // namespace magda::velocity_lane
