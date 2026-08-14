#include "BindingTransform.hpp"

#include <cmath>

namespace magda {

namespace {
// Cached constants
static const float kE = std::exp(1.0f);
static const float kEMinus1 = kE - 1.0f;
}  // namespace

// ============================================================================
// applyMode
// ============================================================================

TransformOutput applyMode(BindingMode mode, TransformInput input) {
    const int v = input.rawValue;
    const int vmax = input.rawMax > 0 ? input.rawMax : 127;

    switch (mode) {
        case BindingMode::Absolute: {
            float normalized = static_cast<float>(v) / static_cast<float>(vmax);
            return {true, normalized};
        }

        case BindingMode::Relative2sComp: {
            // Values 0..63 = positive, 64..127 = negative (two's complement on 7 bits)
            int signed_val = (v < 64) ? v : v - 128;
            float delta = static_cast<float>(signed_val) / 64.0f;
            return {false, delta};
        }

        case BindingMode::RelativeSignMag: {
            // Bit 6 (0x40) = sign: 1 means decrement, 0 means increment
            // Bits 5..0 (0x3F) = magnitude
            bool negative = (v & 0x40) != 0;
            int magnitude = v & 0x3F;
            float delta = static_cast<float>(magnitude) / 63.0f;
            if (negative)
                delta = -delta;
            return {false, delta};
        }

        case BindingMode::RelativeBinOff: {
            // 64 = no change, <64 = decrement, >64 = increment
            float delta = static_cast<float>(v - 64) / 64.0f;
            return {false, delta};
        }

        case BindingMode::Toggle: {
            // Toggle is handled via applyToggle(); return absolute 0 as placeholder.
            // Callers should call applyToggle() directly for this mode.
            return {true, 0.0f};
        }
    }

    // Unreachable, but avoids compiler warning
    return {true, 0.0f};
}

// ============================================================================
// applyCurve
// ============================================================================

float applyCurve(BindingCurve curve, float x) {
    // Clamp input to [0,1]
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);

    switch (curve) {
        case BindingCurve::Linear:
            return x;

        case BindingCurve::Log:
            // log1p(x * (e - 1)) maps [0,1] -> [0,1] with log shape
            return std::log1p(x * kEMinus1);

        case BindingCurve::Exp:
            // expm1(x) / (e - 1) maps [0,1] -> [0,1] with exp shape
            return std::expm1(x) / kEMinus1;

        case BindingCurve::SCurve:
            // Smoothstep: x*x*(3 - 2*x)
            return x * x * (3.0f - 2.0f * x);
    }

    return x;
}

// ============================================================================
// applyRange
// ============================================================================

float applyRange(const BindingRange& range, float normalizedAfterCurve) {
    return range.min + normalizedAfterCurve * (range.max - range.min);
}

// ============================================================================
// Inverses
// ============================================================================

float invertCurve(BindingCurve curve, float y) {
    y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);

    switch (curve) {
        case BindingCurve::Linear:
            return y;

        case BindingCurve::Log:
            // The inverse of log1p(x * (e - 1)), which is the Exp curve itself.
            return std::expm1(y) / kEMinus1;

        case BindingCurve::Exp:
            // And its mirror: Exp and Log are each other's inverse.
            return std::log1p(y * kEMinus1);

        case BindingCurve::SCurve: {
            // Smoothstep is the cubic 3x^2 - 2x^3, whose one real root in [0,1]
            // has this closed form. Cheaper and exact where a Newton iteration
            // would be neither, and it lands exactly on 0, 0.5 and 1.
            const float clamped = juce::jlimit(-1.0f, 1.0f, 1.0f - 2.0f * y);
            return 0.5f - std::sin(std::asin(clamped) / 3.0f);
        }
    }

    return y;
}

float invertRange(const BindingRange& range, float ranged) {
    const float span = range.max - range.min;
    if (span == 0.0f)
        return 0.0f;
    return juce::jlimit(0.0f, 1.0f, (ranged - range.min) / span);
}

// ============================================================================
// applyToggle
// ============================================================================

float applyToggle(int rawValue, ToggleState& state) {
    bool isHigh = (rawValue >= 64);

    // Rising edge: was low, now high
    if (isHigh && !state.wasHigh) {
        state.on = !state.on;
    }

    state.wasHigh = isHigh;

    return state.on ? 1.0f : 0.0f;
}

// ============================================================================
// applyModeNormalized
// ============================================================================

TransformOutput applyModeNormalized(BindingMode mode, float normalized, ToggleState& state) {
    const float clamped = juce::jlimit(0.0f, 1.0f, normalized);

    if (mode == BindingMode::Toggle) {
        // Reuses the MIDI edge logic rather than restating it, by handing it a
        // value on the same side of its threshold. Toggle only asks which side
        // of half-way the control is, so nothing is lost in the conversion and
        // the two paths cannot drift apart.
        return {true, applyToggle(clamped >= 0.5f ? 127 : 0, state)};
    }

    // Absolute, and the relative modes with it — see the header for why a
    // value that arrives normalized has no delta to decode.
    return {true, clamped};
}

}  // namespace magda
