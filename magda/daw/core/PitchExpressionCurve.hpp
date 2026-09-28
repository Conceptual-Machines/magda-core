#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "ClipInfo.hpp"
#include "CurveMath.hpp"

/**
 * @file PitchExpressionCurve.hpp
 * @brief What a note's MPE pitch glide reads at a given beat (#2198).
 *
 * Three places used to answer this, each with its own copy of the same linear
 * interpolation: the piano roll drew one curve, MidiClipCompiler compiled a
 * second, and ClipSynchronizer handed a third to Tracktion. They agreed only
 * because none of them had a shape to disagree about. Giving the segments a
 * tension ends that, so the answer lives here and all three ask it.
 *
 * The shape itself is `curvemath::evalSegment`, which is what automation lanes,
 * the LFO editor and the CC lane already bend by. A glide is not a different
 * kind of curve from those and should not be a different kind of maths.
 *
 * Pure: no allocation beyond the caller's, no locks, safe on the audio thread.
 */

namespace magda {

/**
 * @brief Value of @p points at @p relBeat, in semitones.
 *
 * @p points must be sorted by beat. Outside the authored range the curve holds
 * its end values rather than extrapolating: a glide is a shape drawn on a note,
 * and a note that begins before its first point sounds at that point's pitch.
 */
inline double evaluatePitchExpressionCurve(const std::vector<MidiPitchExpressionPoint>& points,
                                           double relBeat) {
    if (points.empty())
        return 0.0;
    if (relBeat <= points.front().beat)
        return points.front().semitones;
    if (relBeat >= points.back().beat)
        return points.back().semitones;

    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (relBeat < a.beat || relBeat > b.beat)
            continue;

        const double span = b.beat - a.beat;
        if (span <= 0.0)
            return b.semitones;

        const double t = (relBeat - a.beat) / span;
        if (std::abs(a.tension) < 0.001)
            return a.semitones + t * (b.semitones - a.semitones);

        // No stored shaper: a glide bends by the tension scalar alone, the way
        // the tempo lane does. Handles would be a second way to say the same
        // thing and a second thing to serialise.
        return static_cast<double>(curvemath::evalSegment(
            static_cast<float>(a.semitones), static_cast<float>(b.semitones), 0.0f,
            static_cast<float>(a.tension), false, static_cast<float>(t)));
    }

    return points.back().semitones;
}

/**
 * @brief Whether the segment starting at @p index can take a bend at all.
 *
 * A segment between two points at the same pitch cannot: the shape is a warp of
 * the travel between the endpoints and there is none, so `evalSegment` returns
 * the flat value whatever tension it is handed. Same property the automation
 * lanes and the LFO editor have.
 *
 * Asked by the hit test as well as by the drag, from here rather than twice,
 * because the two have to draw the line in the same place and with the same
 * epsilon. When they did not, a flat segment advertised the bend cursor,
 * accepted the gesture, did nothing with it, and still committed an undo entry
 * on release (#2198 review).
 */
inline bool pitchExpressionSegmentCanBend(const std::vector<MidiPitchExpressionPoint>& points,
                                          std::size_t index) {
    if (index + 1 >= points.size())
        return false;

    const auto& left = points[index];
    const auto& right = points[index + 1];
    return (right.beat - left.beat) > 0.0 && std::abs(right.semitones - left.semitones) >= 1.0e-6;
}

/// The range a tension may take, matching every other curve in the app.
inline constexpr double kMaxPitchExpressionTension = 3.0;

/**
 * @brief The tension whose curve passes through @p valueRatio at @p t.
 *
 * The inverse of what `evalSegment` does, so a drag can say "bend until the
 * curve is under my cursor" rather than nudging an opaque number until it looks
 * right. @p t is where along the segment the cursor is (0 at the left point, 1
 * at the right), @p valueRatio where its value sits between the two endpoints
 * (0 at the left value, 1 at the right).
 *
 * Both are pulled away from the ends before inverting: at t=0 and t=1 every
 * curve passes through the same place, so the question has no answer there and
 * the logs would run away trying to find one.
 */
inline double pitchExpressionTensionThrough(double t, double valueRatio) {
    constexpr double kEps = 1.0e-4;
    const double tt = std::clamp(t, 0.05, 0.95);
    const double w = std::clamp(valueRatio, kEps, 1.0 - kEps);

    // r is the curve's value ratio at the segment's midpoint, which is the one
    // number evalSegment's warp is really parameterised by.
    double r = 0.0;
    if (w <= tt) {
        // Below the straight line: warp(t) = t^k, k = log(r)/log(0.5).
        const double k = std::log(w) / std::log(tt);
        r = std::pow(0.5, k);
    } else {
        // Above it, and the mirror image of the same power.
        const double m = std::log(1.0 - w) / std::log(1.0 - tt);
        r = 1.0 - std::pow(0.5, m);
    }
    r = std::clamp(r, kEps, 1.0 - kEps);

    const double tension = (r <= 0.5) ? (std::log(r) / std::log(0.5) - 1.0) * 0.5
                                      : (1.0 - std::log(1.0 - r) / std::log(0.5)) * 0.5;

    return std::clamp(tension, -kMaxPitchExpressionTension, kMaxPitchExpressionTension);
}

}  // namespace magda
