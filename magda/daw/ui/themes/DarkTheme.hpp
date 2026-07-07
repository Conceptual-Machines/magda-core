#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace magda {

// Converts an OKLCH color (perceptual lightness/chroma/hue) to a JUCE sRGB
// Colour. Used to derive track swatches that hold lightness and chroma
// constant while only hue rotates, so every track sits at the same visual
// energy regardless of which hue it lands on.
inline juce::Colour oklchToColour(float L, float C, float hueDegrees, float alpha = 1.0f) {
    const float hue = juce::degreesToRadians(hueDegrees);
    const float a = C * std::cos(hue);
    const float b = C * std::sin(hue);

    const float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    const float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    const float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    const float rLin = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    const float gLin = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    const float bLin = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    auto toSrgb = [](float c) {
        c = juce::jlimit(0.0f, 1.0f, c);
        c = c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        return static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, c * 255.0f + 0.5f));
    };

    return juce::Colour(
        toSrgb(rLin), toSrgb(gLin), toSrgb(bLin),
        static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f, alpha * 255.0f + 0.5f)));
}

// Converts a JUCE sRGB Colour to its true OKLCH hue in degrees. NOTE: this is
// NOT the same angle as juce::Colour::getHue() (HSL hue) — HSL and OKLCH hue
// wheels point in different directions for the same colour, so the two must
// never be substituted for each other.
inline float sRgbToOklchHue(juce::Colour colour) {
    auto toLinear = [](float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    const float r = toLinear(colour.getFloatRed());
    const float g = toLinear(colour.getFloatGreen());
    const float b = toLinear(colour.getFloatBlue());

    const float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    const float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    const float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);

    const float a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    const float bLab = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    return juce::radiansToDegrees(std::atan2(bLab, a));
}

// Derives a track's header/clip swatch from its stored (user-picked) colour:
// same hue, but normalized to a fixed lightness and chroma so every track
// reads at the same visual energy. Colours near the "no colour" sentinel
// (near-zero saturation) pass through unchanged.
inline juce::Colour deriveTrackSwatch(juce::Colour stored, float alpha = 1.0f) {
    if (stored.getSaturation() < 0.05f)
        return stored.withAlpha(alpha);
    return oklchToColour(0.55f, 0.10f, sRgbToOklchHue(stored), alpha);
}

/**
 * SideFX-inspired dark theme for MAGDA
 * Color palette derived from professional audio production interfaces
 */
class DarkTheme {
  public:
    // ==========================================================================
    // Elevation ramp — each layer is one fixed value; depth comes from the
    // step between layers, not from per-component tuning.
    // ==========================================================================
    static constexpr auto E0 = 0xFF0C0F14;  // App background
    static constexpr auto E1 = 0xFF151A21;  // Panel
    static constexpr auto E2 = 0xFF1E242D;  // Control / card
    static constexpr auto E3 = 0xFF28303A;  // Raised / hover
    static constexpr auto HAIRLINE = 0xFF2C343E;

    // Background colors — all panel-tier surfaces share E1 so the app stops
    // reading as a stack of near-identical greys.
    static constexpr auto BACKGROUND = E0;        // Main background
    static constexpr auto BACKGROUND_ALT = E1;    // Charcoal panel tier
    static constexpr auto PANEL_BACKGROUND = E1;  // Panel background
    static constexpr auto SURFACE = E2;           // Elevated surface
    static constexpr auto SURFACE_HOVER = E3;     // Hovered surface

    // ==========================================================================
    // Transport and controls
    // ==========================================================================
    static constexpr auto TRANSPORT_BACKGROUND = E1;        // Transport bar background
    static constexpr auto BUTTON_NORMAL = E0;               // Normal button (off state)
    static constexpr auto BUTTON_HOVER = E2;                // Hovered button
    static constexpr auto BUTTON_PRESSED = E3;              // Pressed button
    static constexpr auto BUTTON_ACTIVE = 0xFF5588AA;       // Active/selected button (SideFX blue)
    static constexpr auto BUTTON_STROKE = HAIRLINE;         // Button border
    static constexpr auto CONTROL_VALUE_FILL = 0x12E8EDF1;  // Neutral value fill
    static constexpr auto CONTROL_SLIDER_THUMB = 0xFF999999;  // Neutral slider thumb

    // ==========================================================================
    // Text colors
    // ==========================================================================
    static constexpr auto TEXT_PRIMARY = 0xFFE8EDF1;    // Primary text (soft white)
    static constexpr auto TEXT_SECONDARY = 0xFFAEB6BD;  // Secondary text
    static constexpr auto TEXT_DIM = 0xFF7C858D;        // Dimmed / tertiary text
    static constexpr auto TEXT_DISABLED = 0xFF666666;   // Disabled text

    // ==========================================================================
    // Accent colors
    // ==========================================================================
    static constexpr auto ACCENT_BLUE = 0xFF5588AA;        // Primary accent (muted blue)
    static constexpr auto ACCENT_BLUE_LIGHT = 0xFF88AACC;  // Light blue
    static constexpr auto ACCENT_CYAN = 0xFF66AAFF;        // Cyan (selection, highlight)
    static constexpr auto ACCENT_GREEN = 0xFF43C07A;       // Bright green (enabled/monitor)
    static constexpr auto ACCENT_ORANGE =
        0xFFFF8822;  // Orange — transport chrome + mod type colour
    static constexpr auto ACCENT_PURPLE =
        0xFF7777DD;  // Purple — transport chrome + macro type colour
    static constexpr auto MASTER_TRACK_COLOUR = 0xFF6655AA;  // Master track (muted purple)

    // ==========================================================================
    // Status colors
    // ==========================================================================
    static constexpr auto STATUS_SUCCESS = 0xFF44AA44;  // Success/enabled (green)
    static constexpr auto STATUS_WARNING = 0xFFFFAA44;  // Warning (orange)
    static constexpr auto STATUS_ERROR = 0xFFAA4444;    // Error (muted red)
    static constexpr auto STATUS_DANGER = 0xFFE64343;   // Danger/record

    // Backwards compatibility aliases
    static constexpr auto ACCENT_RED = STATUS_DANGER;  // Alias for STATUS_DANGER

    // ==========================================================================
    // Track colors
    // ==========================================================================
    static constexpr auto TRACK_BACKGROUND = E1;  // Track background
    static constexpr auto TRACK_SELECTED = E2;    // Selected track (timeline lane tint)
    // Selected track header/strip fill, shared by the arrange headers, mixer
    // and session views. Near-white: selection inverts the header instead of
    // shifting it a step on the dark ramp, so it is unmissable and the dark
    // control chips gain contrast. Text on it flips to
    // TRACK_HEADER_SELECTED_TEXT.
    static constexpr auto TRACK_HEADER_SELECTED = 0xFFB4BCC6;
    static constexpr auto TRACK_HEADER_SELECTED_TEXT = E0;
    static constexpr auto TRACK_SEPARATOR = E0;  // Track separator lines (flush with background)

    // ==========================================================================
    // Timeline and grid
    // ==========================================================================
    static constexpr auto TIMELINE_BACKGROUND = E1;  // Timeline background
    static constexpr auto GRID_LINE = 0xFF383840;    // Grid lines
    static constexpr auto BEAT_LINE = 0xFF484850;    // Beat lines (stronger)
    static constexpr auto BAR_LINE = 0xFF555555;     // Bar lines (strongest)

    // ==========================================================================
    // Borders and separators
    // ==========================================================================
    static constexpr auto BORDER = HAIRLINE;     // General borders
    static constexpr auto SEPARATOR = HAIRLINE;  // Panel separators
    static constexpr auto RESIZE_HANDLE =
        0xFF3A4450;  // Resize handles (a step brighter than hairline)

    // ==========================================================================
    // Audio visualization
    // ==========================================================================
    static constexpr auto WAVEFORM_NORMAL = 0xFF33E680;     // Waveform color (green)
    static constexpr auto WAVEFORM_SELECTED = 0xFF66AAFF;   // Selected waveform (cyan)
    static constexpr auto LEVEL_METER_GREEN = 0xFF44AA44;   // Level meter (low)
    static constexpr auto LEVEL_METER_YELLOW = 0xFFFFAA44;  // Level meter (mid)
    static constexpr auto LEVEL_METER_RED = 0xFFAA4444;     // Level meter (high)

    // ==========================================================================
    // Selection and loop regions
    // ==========================================================================
    static constexpr auto TIME_SELECTION = 0x335588AA;  // Semi-transparent blue for time selection
    static constexpr auto LOOP_REGION = 0x08FFFFFF;     // Nearly transparent white for loop region
    static constexpr auto LOOP_MARKER = 0xFF44AA66;     // Solid green for loop flag markers
    static constexpr auto OFFSET_MARKER = 0xFFCCAA44;   // Solid yellow for content offset marker

    // Apply the theme to JUCE's LookAndFeel
    static void applyToLookAndFeel(juce::LookAndFeel_V4& laf);

    // Get color as JUCE Colour object
    static juce::Colour getColour(juce::uint32 colorValue) {
        return juce::Colour(colorValue);
    }

    // Helper methods for common color combinations
    static juce::Colour getBackgroundColour() {
        return getColour(BACKGROUND);
    }
    static juce::Colour getPanelBackgroundColour() {
        return getColour(PANEL_BACKGROUND);
    }
    static juce::Colour getTextColour() {
        return getColour(TEXT_PRIMARY);
    }
    static juce::Colour getSecondaryTextColour() {
        return getColour(TEXT_SECONDARY);
    }
    static juce::Colour getAccentColour() {
        return getColour(ACCENT_BLUE);
    }
    static juce::Colour getBorderColour() {
        return getColour(BORDER);
    }
};

}  // namespace magda
