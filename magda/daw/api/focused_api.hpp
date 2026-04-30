#pragma once

#include <juce_core/juce_core.h>

namespace magda {

/**
 * Read/write surface for the currently-focused device's macros.
 *
 * "Focused" follows MAGDA's existing focus model — the device whose macro
 * page is showing in the bottom panel, with rack-automap promotion (focusing
 * a rack engages its rack-level macros). Same notion already used by the
 * static binding system's `focused.macro` resolver.
 *
 * Macro indices are 0..15 (two pages of 8). Values are normalized 0..1.
 *
 * When nothing is focused, all reads return safe defaults (empty string,
 * 0.0, false) and writes are no-ops.
 */
class FocusedApi {
  public:
    virtual ~FocusedApi() = default;

    /** True iff there's a focused macro owner (device or rack). */
    virtual bool hasFocus() const = 0;

    /** Display name of the focused device / rack, or "" if no focus. */
    virtual juce::String getFocusedName() const = 0;

    /** Name of macro `idx` on the focused owner. "" if no focus / OOB. */
    virtual juce::String getMacroName(int idx) const = 0;

    /** Current normalized value of macro `idx`. 0 if no focus / OOB. */
    virtual float getMacroValue(int idx) const = 0;

    /** Write a normalized value to macro `idx`. No-op if no focus / OOB. */
    virtual void setMacroValue(int idx, float value) = 0;

    /** Automap-style helper: for each macro 0..7 on the focused device that
     *  isn't already linked to anything, link it to the corresponding
     *  automatable parameter (idx 0 -> param 0, idx 1 -> param 1, ...) at
     *  amount 1.0 unipolar, naming the macro after the param. Idempotent —
     *  re-running won't overwrite user-set links. No-op if no focus, or if
     *  the focused node isn't a plugin device. */
    virtual void autoMapToFirstParams() = 0;
};

}  // namespace magda
