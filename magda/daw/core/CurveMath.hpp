#pragma once

#include <algorithm>
#include <cmath>

// Single source of truth for how a bent curve segment is shaped, so the editor
// render, the modulator engine (output circle / mini preview), and the lock-free
// audio snapshot all produce the SAME curve. Pure math: no allocation, no locks,
// safe to call from the audio thread.
namespace magda::curvemath {

// Value of a Linear-type segment at parameter t in [0,1], between endpoint values
// y1 and y2.
//
//  - hasStoredShaper: the bend is encoded by a quadratic control point (LFO curve
//    modulator, automation lanes). controlY is that control's Y. We render a
//    BOUNDED, monotonic power warp through the on-curve midpoint, so the curve
//    flattens against the [0,1] boundary instead of overshooting like a raw
//    quadratic. Dragging the handle past an endpoint keeps increasing the bend.
//  - otherwise: the bend is a single tension scalar (tempo lane). Same warp,
//    parameterised by tension directly (identical to the legacy applyTension).
inline float evalSegment(float y1, float y2, float controlY, float tension, bool hasStoredShaper,
                         float t) {
    const float dy = y2 - y1;
    if (std::abs(dy) < 1.0e-6f)
        return y1;  // flat segment

    float r;  // curve value at t=0.5, normalised within [y1, y2]
    if (hasStoredShaper) {
        // On-curve midpoint of the encoding quadratic: B(0.5) = 0.25 y1 + 0.5 C + 0.25 y2.
        const float midY = 0.25f * y1 + 0.5f * controlY + 0.25f * y2;
        const float rRaw = (midY - y1) / dy;

        // Fold values outside (0,1) so dragging past an endpoint keeps bending
        // instead of capping at the boundary.
        constexpr float kEps = 1.0e-4f;
        if (rRaw <= 0.0f)
            r = kEps * std::exp(rRaw * 5.0f);
        else if (rRaw >= 1.0f)
            r = 1.0f - kEps * std::exp((1.0f - rRaw) * 5.0f);
        else
            r = std::clamp(rRaw, kEps, 1.0f - kEps);
    } else {
        if (std::abs(tension) < 0.001f)
            return y1 + t * dy;  // pure linear
        // Map the tension scalar to the same r the power warp uses at t=0.5.
        r = (tension > 0.0f) ? std::pow(0.5f, 1.0f + tension * 2.0f)
                             : 1.0f - std::pow(0.5f, 1.0f - tension * 2.0f);
        r = std::clamp(r, 1.0e-4f, 1.0f - 1.0e-4f);
    }

    const float inv = 1.0f / std::log(0.5f);
    const float warp = (r <= 0.5f) ? std::pow(t, std::log(r) * inv)
                                   : 1.0f - std::pow(1.0f - t, std::log(1.0f - r) * inv);
    return y1 + dy * warp;
}

}  // namespace magda::curvemath
