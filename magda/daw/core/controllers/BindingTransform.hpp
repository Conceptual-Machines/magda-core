#pragma once

#include "Binding.hpp"

namespace magda {

// ============================================================================
// Transform I/O types
// ============================================================================

struct TransformInput {
    int rawValue;  // 0..rawMax (for CC: 0..127; for NRPN: 0..16383)
    int rawMax;    // maximum value for this message type
};

struct TransformOutput {
    bool isAbsolute;  // true: value is absolute in [0,1]; false: signed delta in [-1,1]
    float value;
};

// ============================================================================
// Pure transform functions
// ============================================================================

/**
 * @brief Apply the binding mode to a raw MIDI value.
 *
 * Absolute:        value = rawValue / rawMax, isAbsolute = true.
 * Relative2sComp:  signed = (v < 64) ? v : v - 128; delta = signed / 64, isAbsolute = false.
 * RelativeSignMag: sign bit = 0x40, mag = 0x3F; delta = +/-mag/63, isAbsolute = false.
 * RelativeBinOff:  delta = (v - 64) / 64, isAbsolute = false.
 * Toggle:          see applyToggle() -- use this function for the rising-edge logic.
 */
TransformOutput applyMode(BindingMode mode, TransformInput input);

/**
 * @brief Apply a curve to a normalized value in [0,1].
 *
 * Linear: y = x
 * Log:    y = log1p(x * (e - 1))
 * Exp:    y = expm1(x) / (e - 1)
 * SCurve: y = x * x * (3 - 2 * x)  (smoothstep)
 */
float applyCurve(BindingCurve curve, float normalized);

/**
 * @brief Map a normalized-after-curve value into a BindingRange.
 *
 * result = range.min + normalizedAfterCurve * (range.max - range.min)
 */
float applyRange(const BindingRange& range, float normalizedAfterCurve);

// ============================================================================
// ToggleState
// ============================================================================

/**
 * @brief Persistent state for Toggle mode, carried per binding by the caller.
 *
 * - on:      whether the toggle output is currently "on" (true = 1.0).
 * - wasHigh: whether the previous raw value was >= 64 (used to suppress
 *            re-triggering on consecutive high values).
 */
struct ToggleState {
    bool on = false;
    bool wasHigh = false;
};

/**
 * @brief Apply Toggle mode.
 *
 * Flips state.on on a rising edge (rawValue >= 64 after previously < 64).
 * No re-trigger if the value stays >= 64. Returns 1.0f when on, 0.0f when off.
 * The caller applies range afterward via applyRange().
 *
 * @param rawValue Current raw MIDI value.
 * @param state    In/out: persistent toggle state (on + wasHigh).
 * @return 1.0f when state.on is true, 0.0f otherwise.
 */
float applyToggle(int rawValue, ToggleState& state);

}  // namespace magda
