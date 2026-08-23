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
// Inverses
// ============================================================================

/**
 * @brief Recover the position a control must be at to produce `curved`.
 *
 * `invertCurve(c, applyCurve(c, x)) == x` for every curve and every x in
 * [0,1]. OSC feedback (#2091) needs it: a bound address echoes where the
 * surface's fader should sit, and what the model holds is the value at the far
 * end of the curve.
 *
 * Every curve here is monotonic on [0,1], so each has one inverse and this is
 * total rather than a best effort.
 */
float invertCurve(BindingCurve curve, float curved);

/**
 * @brief Recover the normalized-after-curve value that `applyRange` produced.
 *
 * A degenerate range — min equal to max — maps every position onto one value
 * and therefore inverts to none of them. It answers 0, which is the position a
 * fader sits at when nothing it can do changes anything.
 */
float invertRange(const BindingRange& range, float ranged);

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

/**
 * @brief Apply a binding mode to a value that arrived already normalized.
 *
 * The entry point for OSC sources, which send a float in [0,1] rather than a
 * 7-bit step. Routing those through `applyMode` would mean quantising a fader
 * to 128 positions on the way in and then dividing it back out — a resolution
 * loss with nothing to show for it.
 *
 * Absolute passes the value through, clamped. Toggle flips on the rising edge
 * across the half-way point, sharing `ToggleState` with the MIDI path so a
 * parameter bound from either behaves identically.
 *
 * The relative modes fall back to absolute. They decode a *delta* out of a
 * 7-bit word — two's complement, sign-and-magnitude, binary offset — and a
 * value that arrives as a position on a 0..1 fader carries no such encoding.
 * There is nothing to decode, so there is nothing to honour; the OSC learn
 * path never produces them, and the settings UI does not offer them for an OSC
 * source.
 */
TransformOutput applyModeNormalized(BindingMode mode, float normalized, ToggleState& state);

}  // namespace magda
