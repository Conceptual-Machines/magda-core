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

}  // namespace magda
