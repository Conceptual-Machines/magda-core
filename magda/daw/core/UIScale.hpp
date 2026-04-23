#pragma once

#include <array>

namespace magda {

// Allowed scale factors for keyboard cycling. The Preferences combo offers the
// same set plus "Auto" (resolved at startup from display DPI).
inline constexpr std::array<double, 5> kUIScaleSteps = {1.0, 1.25, 1.5, 1.75, 2.0};

// Sane bounds for any scale factor we apply — guards against a fat-fingered
// env var or stale config locking the user out by making everything microscopic
// or huge.
inline constexpr double kMinUIScale = 0.5;
inline constexpr double kMaxUIScale = 4.0;

// Resolve the scale to use at app startup.
// Precedence: MAGDA_UI_SCALE env var > Config::getUIScale() > auto from DPI.
// Returns a value in [kMinUIScale, kMaxUIScale]. Safe to call before any window
// exists (will fall back to 1.0 if no display is queryable).
double resolveStartupScale();

// Auto scale derived purely from the primary display's DPI — ignores env var
// and Config. Use when the user explicitly picked "Auto" in the UI and expects
// DPI-based behavior, not the startup precedence chain.
double dpiOnlyAutoScale();

// Apply a scale factor at runtime. Calls juce::Desktop::setGlobalScaleFactor
// and triggers a top-level repaint so existing components pick up the new
// scale immediately. When `persist` is true (default), also writes the value
// to Config and saves. Pass `persist=false` when the caller wants to manage
// Config writes itself (e.g. the "Auto" path that needs to store 0 as a
// sentinel rather than the resolved value).
void applyUIScale(double scale, bool persist = true);

// Pick the next/previous step from kUIScaleSteps relative to the current
// global scale factor. direction = +1 increases, -1 decreases. Clamped to the
// ends of the table.
double stepUIScale(double current, int direction);

}  // namespace magda
