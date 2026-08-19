#include "AutomationCurve.hpp"

#include <algorithm>
#include <cmath>

#include "CurveMath.hpp"

namespace magda::automation {

namespace {

double interpolateBezier(double t, const AutomationPoint& p1, const AutomationPoint& p2) {
    // The editor renders the PARAMETRIC cubic (handles offset the control
    // points in x too), so evaluating the value cubic at t-linear-in-x
    // drifts off the drawn line whenever a handle has a beat offset. Solve
    // the x cubic for the parameter s where the curve crosses the queried
    // beat, then evaluate the value cubic there.
    const double x0 = p1.beatPosition;
    const double x3 = p2.beatPosition;
    const double x1 = x0 + p1.outHandle.beatOffset;
    const double x2 = x3 + p2.inHandle.beatOffset;
    const double target = x0 + t * (x3 - x0);

    const auto xAt = [&](double s) {
        const double ms = 1.0 - s;
        return ms * ms * ms * x0 + 3.0 * ms * ms * s * x1 + 3.0 * ms * s * s * x2 + s * s * s * x3;
    };
    const auto dxAt = [&](double s) {
        const double ms = 1.0 - s;
        return 3.0 * ms * ms * (x1 - x0) + 6.0 * ms * s * (x2 - x1) + 3.0 * s * s * (x3 - x2);
    };

    // Newton with a t seed; clamped so overshooting handles can't escape the
    // segment. A handful of iterations reaches sub-sample accuracy.
    double s = t;
    for (int i = 0; i < 12; ++i) {
        const double err = xAt(s) - target;
        if (std::abs(err) < 1.0e-6 * (x3 - x0 + 1.0))
            break;
        const double slope = dxAt(s);
        if (std::abs(slope) < 1.0e-9)
            break;
        s = std::clamp(s - err / slope, 0.0, 1.0);
    }

    const double s2 = s * s;
    const double s3 = s2 * s;
    const double ms = 1.0 - s;
    const double ms2 = ms * ms;
    const double ms3 = ms2 * ms;
    const double cp1Value = p1.value + p1.outHandle.value;
    const double cp2Value = p2.value + p2.inHandle.value;
    return ms3 * p1.value + 3.0 * ms2 * s * cp1Value + 3.0 * ms * s2 * cp2Value + s3 * p2.value;
}

// Linear-type segments (pure, tension, or shaper-bent) evaluate through the
// SHARED curvemath::evalSegment — the same function the editor samples when
// drawing and the modulator engine outputs — so the played value sits exactly
// on the drawn curve. (The old local quadratic/power copies here had drifted
// from the renderer.) t is the x-fraction along the segment.
double evalLinearSegment(double t, const AutomationPoint& p1, const AutomationPoint& p2) {
    const bool hasShaper = !p1.outHandle.isZero() || !p2.inHandle.isZero();
    const double controlY = p1.value + p1.outHandle.value;
    return static_cast<double>(curvemath::evalSegment(
        static_cast<float>(p1.value), static_cast<float>(p2.value), static_cast<float>(controlY),
        static_cast<float>(p1.tension), hasShaper, static_cast<float>(t)));
}

}  // namespace

double valueAtBeat(std::span<const AutomationPoint> points, double beatPosition) {
    if (points.empty())
        return 0.5;

    // Before first point
    if (beatPosition <= points.front().beatPosition)
        return points.front().value;

    // After last point
    if (beatPosition >= points.back().beatPosition)
        return points.back().value;

    // Find surrounding points
    for (size_t i = 0; i < points.size() - 1; ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];

        if (beatPosition >= p1.beatPosition && beatPosition < p2.beatPosition) {
            // Normalize t to 0-1 between points
            double duration = p2.beatPosition - p1.beatPosition;
            if (duration <= 0.0)
                return p1.value;

            double t = (beatPosition - p1.beatPosition) / duration;

            switch (p1.curveType) {
                case AutomationCurveType::Linear:
                    return evalLinearSegment(t, p1, p2);

                case AutomationCurveType::Bezier:
                    return interpolateBezier(t, p1, p2);

                case AutomationCurveType::Step:
                    return p1.value;  // Hold until next point

                case AutomationCurveType::HardCorner: {
                    // Two straight segments meeting at the apex (from the
                    // shaper handle), or the midpoint when no apex was dragged.
                    double apexT = 0.5;
                    double apexValue = (p1.value + p2.value) * 0.5;
                    if (!p1.outHandle.isZero()) {
                        apexT =
                            juce::jlimit(1.0e-4, 1.0 - 1.0e-4, p1.outHandle.beatOffset / duration);
                        apexValue = p1.value + p1.outHandle.value;
                    }
                    if (t < apexT)
                        return p1.value + (t / apexT) * (apexValue - p1.value);
                    return apexValue + ((t - apexT) / (1.0 - apexT)) * (p2.value - apexValue);
                }
            }
        }
    }

    return 0.5;
}

double laneValueAtBeat(const AutomationLaneInfo& lane,
                       const std::function<const AutomationClipInfo*(AutomationClipId)>& getClip,
                       double beatPosition) {
    if (lane.isAbsolute())
        return valueAtBeat(lane.absolutePoints, beatPosition);

    // Clip-based: find clip containing beat position
    for (auto clipId : lane.clipIds) {
        const auto* clip = getClip(clipId);
        if (clip && clip->containsBeat(beatPosition)) {
            double localBeatPosition = clip->getLocalBeat(beatPosition);
            return valueAtBeat(clip->points, localBeatPosition);
        }
    }

    // Gap between clips: hold the nearest clip edge — the previous clip's
    // final value, or before the first clip, the first clip's initial value.
    const AutomationClipInfo* prevClip = nullptr;  // greatest end <= beat
    const AutomationClipInfo* nextClip = nullptr;  // smallest start > beat
    for (auto clipId : lane.clipIds) {
        const auto* clip = getClip(clipId);
        if (!clip)
            continue;
        if (clip->getEndBeats() <= beatPosition) {
            if (!prevClip || clip->getEndBeats() > prevClip->getEndBeats())
                prevClip = clip;
        } else if (clip->startBeats > beatPosition) {
            if (!nextClip || clip->startBeats < nextClip->startBeats)
                nextClip = clip;
        }
    }
    if (prevClip)
        return valueAtBeat(prevClip->points, prevClip->getEndLocalBeat());
    if (nextClip)
        return valueAtBeat(nextClip->points, 0.0);

    return 0.5;  // Lane has no clips at all
}

}  // namespace magda::automation
