#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace magda::daw::audio::ramp_curve {

/**
 * Quadratic-bezier step-timing warp, shared by StepClock and the MIDI devices.
 *
 * Engine-neutral on purpose (#2299): StepClock itself talks to the current
 * engine's transport, but this curve is what an arpeggiator's Time Bend means,
 * and a MagdaDevice in the neutral pack has to be able to compute it.
 *
 * Maps linear position t (0..1) through a curve whose control point sits at
 * (skew, skew+depth) in graph space. depth 0 is linear; positive bows the
 * curve above the diagonal (front-loaded), negative below (back-loaded).
 * hardAngle swaps the bezier for two straight segments through the control
 * point.
 */
inline double applyRampCurve(double t, float depth, float skew, bool hardAngle = false) {
    auto d = static_cast<double>(juce::jlimit(-0.99f, 0.99f, depth));
    auto s = static_cast<double>(juce::jlimit(0.01, 0.99, 0.5 + static_cast<double>(skew) * 0.49));

    if (std::abs(d) < 0.001)
        return t;

    // Clamp control point ordinate to [0, 1] so the curve stays in bounds
    double cp = juce::jlimit(0.0, 1.0, s + d);

    if (hardAngle) {
        // Piecewise linear: two straight segments through control point (s, cp)
        double result;
        if (t <= s)
            result = t * cp / s;
        else
            result = cp + (t - s) * (1.0 - cp) / (1.0 - s);
        return juce::jlimit(0.0, 1.0, result);
    }

    // Quadratic bezier with control point (s, cp)
    double u;
    double a = 1.0 - 2.0 * s;
    if (std::abs(a) < 1e-10) {
        u = t;
    } else {
        double disc = s * s + a * t;
        u = (-s + std::sqrt(std::max(0.0, disc))) / a;
        u = juce::jlimit(0.0, 1.0, u);
    }

    return juce::jlimit(0.0, 1.0, 2.0 * (1.0 - u) * u * cp + u * u);
}

/** The same curve tiled `cycles` times across [0, 1]. */
inline double applyRampCurveWithCycles(double t, float depth, float skew, int cycles = 1,
                                       bool hardAngle = false) {
    const double clampedT = juce::jlimit(0.0, 1.0, t);
    const int cycleCount = juce::jmax(1, cycles);
    if (cycleCount <= 1)
        return applyRampCurve(clampedT, depth, skew, hardAngle);

    const double segLen = 1.0 / static_cast<double>(cycleCount);
    const int seg = std::min(static_cast<int>(clampedT / segLen), cycleCount - 1);
    const double tLocal = (clampedT - seg * segLen) / segLen;
    return (seg + applyRampCurve(tLocal, depth, skew, hardAngle)) * segLen;
}

}  // namespace magda::daw::audio::ramp_curve
