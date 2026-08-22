#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

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
juce::Colour deriveTrackSwatch(juce::Colour stored, float alpha = 1.0f);

/**
 * SideFX-inspired dark theme for MAGDA
 * Color palette derived from professional audio production interfaces
 */
enum class ColourRole : std::size_t {
    E0,
    E1,
    E2,
    E3,
    HAIRLINE,
    BACKGROUND,
    BACKGROUND_ALT,
    PANEL_BACKGROUND,
    SURFACE,
    SURFACE_HOVER,
    TRANSPORT_BACKGROUND,
    BUTTON_NORMAL,
    BUTTON_HOVER,
    BUTTON_PRESSED,
    BUTTON_ACTIVE,
    BUTTON_STROKE,
    CONTROL_VALUE_FILL,
    CONTROL_SLIDER_THUMB,
    TEXT_PRIMARY,
    TEXT_SECONDARY,
    TEXT_DIM,
    TEXT_DISABLED,
    ACCENT_PRIMARY,
    ACCENT_PRIMARY_SOFT,
    ACCENT_INFO,
    ACCENT_POSITIVE,
    ACCENT_ATTENTION,
    ACCENT_MODULATION,
    PRESET_INDIGO,
    MIDI_LEARN,
    STEP_RECORD,
    MASTER_TRACK_COLOUR,
    STATUS_SUCCESS,
    STATUS_WARNING,
    STATUS_ERROR,
    STATUS_DANGER,
    TRACK_BACKGROUND,
    TRACK_SELECTED,
    TRACK_HEADER_SELECTED,
    TRACK_HEADER_SELECTED_TEXT,
    TRACK_SEPARATOR,
    TIMELINE_BACKGROUND,
    GRID_LINE,
    BEAT_LINE,
    BAR_LINE,
    BORDER,
    SEPARATOR,
    RESIZE_HANDLE,
    WAVEFORM_NORMAL,
    WAVEFORM_SELECTED,
    LEVEL_METER_GREEN,
    LEVEL_METER_YELLOW,
    LEVEL_METER_RED,
    GAIN_METER_LOW,
    GAIN_METER_WARNING,
    GAIN_METER_HIGH,
    GATE_CURVE,
    GATE_THRESHOLD,
    MULTIBAND_LOW,
    MULTIBAND_MID,
    MULTIBAND_HIGH,
    MULTIBAND_LIMIT,
    SAMPLER_START_MARKER,
    SAMPLER_END_MARKER,
    SPECTRUM_OVERLAY,
    INSTRUMENT_BACKGROUND,
    INSTRUMENT_PANEL,
    INSTRUMENT_BORDER,
    INSTRUMENT_TEXT,
    INSTRUMENT_TEXT_DIM,
    TIME_SELECTION,
    LOOP_REGION,
    LOOP_MARKER,
    OFFSET_MARKER,
    TEXT_BRIGHT,
    INPUT_BACKGROUND,
    TOOLTIP_BACKGROUND,
    ICON_NEUTRAL,
    ICON_TRANSPORT,
    ICON_ON_ACCENT,
    AUTOMATION_LANE_BACKGROUND,
    AUTOMATION_LANE_SELECTED,
    AUTOMATION_LANE_HEADER,
    AUTOMATION_LANE_SCALE_BACKGROUND,
    AUTOMATION_DIVIDER,
    AUTOMATION_DIVIDER_LIGHT,
    AUTOMATION_GUIDE,
    AUTOMATION_SCALE_TEXT,
    AUTOMATION_SCALE_LABEL,
    AUTOMATION_TEXT,
    AUTOMATION_POINT,
    AUTOMATION_POINT_HOVER,
    AUTOMATION_BEZIER,
    AUTOMATION_TENSION_HOVER,
    CURVE_BACKGROUND,
    CURVE_TOOLTIP_BACKGROUND,
    CURVE_TOOLTIP_TEXT,
    CURVE_POINT,
    CURVE_HANDLE_BACKGROUND,
    CURVE_HANDLE_NORMAL,
    TEXT_DARK,
    PIANO_ROLL_BACKGROUND,
    PIANO_ROLL_KEY_WHITE,
    PIANO_ROLL_KEY_HIGHLIGHT,
    PIANO_ROLL_KEY_SEPARATOR,
    PIANO_ROLL_PITCH_HIGHLIGHT,
    PIANO_ROLL_GRID_BACKGROUND,
    PIANO_ROLL_GRID_BLACK_KEY,
    PIANO_ROLL_GRID_SUBDIVISION,
    PIANO_ROLL_GRID_BEAT,
    PIANO_ROLL_GRID_BAR,
    PIANO_ROLL_CHORD_PREVIEW,
    PIANO_ROLL_TOOLTIP_BACKGROUND,
    PIANO_ROLL_FALLBACK_CLIP,
    CLIP_BOUNDARY,
    PIANO_ROLL_TAKE_LANE_ACTIVE,
    PIANO_ROLL_TAKE_LANE_INACTIVE,
    TEXT_SLIDER_THUMB,
    TEXT_SLIDER_METER_LOW,
    TEXT_SLIDER_METER_WARNING,
    TEXT_SLIDER_METER_HIGH,
    TOAST_BACKGROUND,
    QWERTY_WHITE_KEY_NOTE_TEXT,
    EQ_BAND_LOW,
    EQ_BAND_LOW_MID,
    EQ_BAND_HIGH_MID,
    EQ_BAND_HIGH,
    ICON_BACKGROUND,
    ICON_BRIGHT,
    ICON_SUBTLE,
    ICON_MODULATION,
    ICON_BRAND,
    ICON_POWER,
    MIXER_FADER_THUMB,
    MIXER_KNOB_OUTER,
    MIXER_KNOB_OUTER_STROKE,
    MIXER_KNOB_INNER,
    MIXER_KNOB_GUIDE,
    count
};

// Syntax colours deliberately live outside the application-surface palette.
// Code editors need their own contrast hierarchy and token meanings, rather
// than inheriting generic text and accent roles.
enum class SyntaxColourRole : std::size_t {
    EDITOR_BACKGROUND,
    EDITOR_DEFAULT_TEXT,
    LINE_NUMBER_BACKGROUND,
    LINE_NUMBER_TEXT,
    EDITOR_CARET,
    DSL_CARET,
    EDITOR_SELECTION,
    DSL_SELECTION,
    DSL_STATUS_BACKGROUND,
    DSL_STATUS_TEXT,
    DSL_OUTPUT_PROMPT,
    DSL_OUTPUT_INFO,
    DSL_OUTPUT_TEXT,
    DSL_OUTPUT_ERROR,
    DSL_TOKEN_ERROR,
    DSL_TOKEN_COMMENT,
    DSL_TOKEN_KEYWORD,
    DSL_TOKEN_METHOD,
    DSL_TOKEN_PARAM,
    DSL_TOKEN_OPERATOR,
    DSL_TOKEN_IDENTIFIER,
    DSL_TOKEN_NUMBER,
    DSL_TOKEN_STRING,
    DSL_TOKEN_BRACKET,
    DSL_TOKEN_PUNCTUATION,
    DSL_TOKEN_NOTE_NAME,
    CHAT_TOKEN_TEXT,
    CHAT_TOKEN_PLUGIN_ALIAS,
    CHAT_TOKEN_PARAM_ALIAS,
    CHAT_TOKEN_SLASH_COMMAND,
    CHAT_TOKEN_PUNCTUATION,
    count
};

class DarkTheme {
  public:
    using Palette = std::array<juce::uint32, static_cast<std::size_t>(ColourRole::count)>;
    using SyntaxPalette =
        std::array<juce::uint32, static_cast<std::size_t>(SyntaxColourRole::count)>;

    // ==========================================================================
    // Elevation ramp — each layer is one fixed value; depth comes from the
    // step between layers, not from per-component tuning.
    // ==========================================================================
    static constexpr auto E0 = ColourRole::E0;
    static constexpr auto E1 = ColourRole::E1;
    static constexpr auto E2 = ColourRole::E2;
    static constexpr auto E3 = ColourRole::E3;
    static constexpr auto HAIRLINE = ColourRole::HAIRLINE;

    // Background colors — all panel-tier surfaces share E1 so the app stops
    // reading as a stack of near-identical greys.
    static constexpr auto BACKGROUND = ColourRole::BACKGROUND;
    static constexpr auto BACKGROUND_ALT = ColourRole::BACKGROUND_ALT;
    static constexpr auto PANEL_BACKGROUND = ColourRole::PANEL_BACKGROUND;
    static constexpr auto SURFACE = ColourRole::SURFACE;
    static constexpr auto SURFACE_HOVER = ColourRole::SURFACE_HOVER;

    // ==========================================================================
    // Transport and controls
    // ==========================================================================
    static constexpr auto TRANSPORT_BACKGROUND = ColourRole::TRANSPORT_BACKGROUND;
    static constexpr auto BUTTON_NORMAL = ColourRole::BUTTON_NORMAL;
    static constexpr auto BUTTON_HOVER = ColourRole::BUTTON_HOVER;
    static constexpr auto BUTTON_PRESSED = ColourRole::BUTTON_PRESSED;
    static constexpr auto BUTTON_ACTIVE = ColourRole::BUTTON_ACTIVE;
    static constexpr auto BUTTON_STROKE = ColourRole::BUTTON_STROKE;
    static constexpr auto CONTROL_VALUE_FILL = ColourRole::CONTROL_VALUE_FILL;
    static constexpr auto CONTROL_SLIDER_THUMB = ColourRole::CONTROL_SLIDER_THUMB;

    // ==========================================================================
    // Text colors
    // ==========================================================================
    static constexpr auto TEXT_PRIMARY = ColourRole::TEXT_PRIMARY;
    static constexpr auto TEXT_SECONDARY = ColourRole::TEXT_SECONDARY;
    static constexpr auto TEXT_DIM = ColourRole::TEXT_DIM;
    static constexpr auto TEXT_DISABLED = ColourRole::TEXT_DISABLED;

    // ==========================================================================
    // Accent colors
    // ==========================================================================
    static constexpr auto ACCENT_PRIMARY = ColourRole::ACCENT_PRIMARY;
    static constexpr auto ACCENT_PRIMARY_SOFT = ColourRole::ACCENT_PRIMARY_SOFT;
    static constexpr auto ACCENT_INFO = ColourRole::ACCENT_INFO;
    static constexpr auto ACCENT_POSITIVE = ColourRole::ACCENT_POSITIVE;
    static constexpr auto ACCENT_ATTENTION = ColourRole::ACCENT_ATTENTION;
    static constexpr auto ACCENT_MODULATION = ColourRole::ACCENT_MODULATION;
    static constexpr auto PRESET_INDIGO = ColourRole::PRESET_INDIGO;
    static constexpr auto MIDI_LEARN = ColourRole::MIDI_LEARN;
    static constexpr auto STEP_RECORD = ColourRole::STEP_RECORD;
    static constexpr auto MASTER_TRACK_COLOUR = ColourRole::MASTER_TRACK_COLOUR;

    // ==========================================================================
    // Status colors
    // ==========================================================================
    static constexpr auto STATUS_SUCCESS = ColourRole::STATUS_SUCCESS;
    static constexpr auto STATUS_WARNING = ColourRole::STATUS_WARNING;
    static constexpr auto STATUS_ERROR = ColourRole::STATUS_ERROR;
    static constexpr auto STATUS_DANGER = ColourRole::STATUS_DANGER;

    // Backwards compatibility aliases
    static constexpr auto ACCENT_RED = STATUS_DANGER;  // Alias for STATUS_DANGER

    // ==========================================================================
    // Track colors
    // ==========================================================================
    static constexpr auto TRACK_BACKGROUND = ColourRole::TRACK_BACKGROUND;
    static constexpr auto TRACK_SELECTED = ColourRole::TRACK_SELECTED;
    // Selected track header/strip fill, shared by the arrange headers, mixer
    // and session views. Near-white: selection inverts the header instead of
    // shifting it a step on the dark ramp, so it is unmissable and the dark
    // control chips gain contrast. Text on it flips to
    // TRACK_HEADER_SELECTED_TEXT.
    static constexpr auto TRACK_HEADER_SELECTED = ColourRole::TRACK_HEADER_SELECTED;
    static constexpr auto TRACK_HEADER_SELECTED_TEXT = ColourRole::TRACK_HEADER_SELECTED_TEXT;
    static constexpr auto TRACK_SEPARATOR = ColourRole::TRACK_SEPARATOR;

    // ==========================================================================
    // Timeline and grid
    // ==========================================================================
    static constexpr auto TIMELINE_BACKGROUND = ColourRole::TIMELINE_BACKGROUND;
    static constexpr auto GRID_LINE = ColourRole::GRID_LINE;
    static constexpr auto BEAT_LINE = ColourRole::BEAT_LINE;
    static constexpr auto BAR_LINE = ColourRole::BAR_LINE;

    // ==========================================================================
    // Borders and separators
    // ==========================================================================
    static constexpr auto BORDER = ColourRole::BORDER;
    static constexpr auto SEPARATOR = ColourRole::SEPARATOR;
    static constexpr auto RESIZE_HANDLE = ColourRole::RESIZE_HANDLE;

    // ==========================================================================
    // Audio visualization
    // ==========================================================================
    static constexpr auto WAVEFORM_NORMAL = ColourRole::WAVEFORM_NORMAL;
    static constexpr auto WAVEFORM_SELECTED = ColourRole::WAVEFORM_SELECTED;
    static constexpr auto LEVEL_METER_GREEN = ColourRole::LEVEL_METER_GREEN;
    static constexpr auto LEVEL_METER_YELLOW = ColourRole::LEVEL_METER_YELLOW;
    static constexpr auto LEVEL_METER_RED = ColourRole::LEVEL_METER_RED;
    static constexpr auto GAIN_METER_LOW = ColourRole::GAIN_METER_LOW;
    static constexpr auto GAIN_METER_WARNING = ColourRole::GAIN_METER_WARNING;
    static constexpr auto GAIN_METER_HIGH = ColourRole::GAIN_METER_HIGH;
    static constexpr auto GATE_CURVE = ColourRole::GATE_CURVE;
    static constexpr auto GATE_THRESHOLD = ColourRole::GATE_THRESHOLD;
    static constexpr auto MULTIBAND_LOW = ColourRole::MULTIBAND_LOW;
    static constexpr auto MULTIBAND_MID = ColourRole::MULTIBAND_MID;
    static constexpr auto MULTIBAND_HIGH = ColourRole::MULTIBAND_HIGH;
    static constexpr auto MULTIBAND_LIMIT = ColourRole::MULTIBAND_LIMIT;
    static constexpr auto SAMPLER_START_MARKER = ColourRole::SAMPLER_START_MARKER;
    static constexpr auto SAMPLER_END_MARKER = ColourRole::SAMPLER_END_MARKER;
    static constexpr auto SPECTRUM_OVERLAY = ColourRole::SPECTRUM_OVERLAY;
    static constexpr auto INSTRUMENT_BACKGROUND = ColourRole::INSTRUMENT_BACKGROUND;
    static constexpr auto INSTRUMENT_PANEL = ColourRole::INSTRUMENT_PANEL;
    static constexpr auto INSTRUMENT_BORDER = ColourRole::INSTRUMENT_BORDER;
    static constexpr auto INSTRUMENT_TEXT = ColourRole::INSTRUMENT_TEXT;
    static constexpr auto INSTRUMENT_TEXT_DIM = ColourRole::INSTRUMENT_TEXT_DIM;

    // ==========================================================================
    // Selection and loop regions
    // ==========================================================================
    static constexpr auto TIME_SELECTION = ColourRole::TIME_SELECTION;
    static constexpr auto LOOP_REGION = ColourRole::LOOP_REGION;
    static constexpr auto LOOP_MARKER = ColourRole::LOOP_MARKER;
    static constexpr auto OFFSET_MARKER = ColourRole::OFFSET_MARKER;

    // ========================================================================
    // Shared UI and automation editor
    // ========================================================================
    static constexpr auto TEXT_BRIGHT = ColourRole::TEXT_BRIGHT;
    static constexpr auto INPUT_BACKGROUND = ColourRole::INPUT_BACKGROUND;
    static constexpr auto TOOLTIP_BACKGROUND = ColourRole::TOOLTIP_BACKGROUND;
    static constexpr auto ICON_NEUTRAL = ColourRole::ICON_NEUTRAL;
    static constexpr auto ICON_TRANSPORT = ColourRole::ICON_TRANSPORT;
    static constexpr auto ICON_ON_ACCENT = ColourRole::ICON_ON_ACCENT;
    static constexpr auto AUTOMATION_LANE_BACKGROUND = ColourRole::AUTOMATION_LANE_BACKGROUND;
    static constexpr auto AUTOMATION_LANE_SELECTED = ColourRole::AUTOMATION_LANE_SELECTED;
    static constexpr auto AUTOMATION_LANE_HEADER = ColourRole::AUTOMATION_LANE_HEADER;
    static constexpr auto AUTOMATION_LANE_SCALE_BACKGROUND =
        ColourRole::AUTOMATION_LANE_SCALE_BACKGROUND;
    static constexpr auto AUTOMATION_DIVIDER = ColourRole::AUTOMATION_DIVIDER;
    static constexpr auto AUTOMATION_DIVIDER_LIGHT = ColourRole::AUTOMATION_DIVIDER_LIGHT;
    static constexpr auto AUTOMATION_GUIDE = ColourRole::AUTOMATION_GUIDE;
    static constexpr auto AUTOMATION_SCALE_TEXT = ColourRole::AUTOMATION_SCALE_TEXT;
    static constexpr auto AUTOMATION_SCALE_LABEL = ColourRole::AUTOMATION_SCALE_LABEL;
    static constexpr auto AUTOMATION_TEXT = ColourRole::AUTOMATION_TEXT;
    static constexpr auto AUTOMATION_POINT = ColourRole::AUTOMATION_POINT;
    static constexpr auto AUTOMATION_POINT_HOVER = ColourRole::AUTOMATION_POINT_HOVER;
    static constexpr auto AUTOMATION_BEZIER = ColourRole::AUTOMATION_BEZIER;
    static constexpr auto AUTOMATION_TENSION_HOVER = ColourRole::AUTOMATION_TENSION_HOVER;
    static constexpr auto CURVE_BACKGROUND = ColourRole::CURVE_BACKGROUND;
    static constexpr auto CURVE_TOOLTIP_BACKGROUND = ColourRole::CURVE_TOOLTIP_BACKGROUND;
    static constexpr auto CURVE_TOOLTIP_TEXT = ColourRole::CURVE_TOOLTIP_TEXT;
    static constexpr auto CURVE_POINT = ColourRole::CURVE_POINT;
    static constexpr auto CURVE_HANDLE_BACKGROUND = ColourRole::CURVE_HANDLE_BACKGROUND;
    static constexpr auto CURVE_HANDLE_NORMAL = ColourRole::CURVE_HANDLE_NORMAL;

    // ========================================================================
    // Piano roll
    // ========================================================================
    static constexpr auto TEXT_DARK = ColourRole::TEXT_DARK;
    static constexpr auto PIANO_ROLL_BACKGROUND = ColourRole::PIANO_ROLL_BACKGROUND;
    static constexpr auto PIANO_ROLL_KEY_WHITE = ColourRole::PIANO_ROLL_KEY_WHITE;
    static constexpr auto PIANO_ROLL_KEY_HIGHLIGHT = ColourRole::PIANO_ROLL_KEY_HIGHLIGHT;
    static constexpr auto PIANO_ROLL_KEY_SEPARATOR = ColourRole::PIANO_ROLL_KEY_SEPARATOR;
    static constexpr auto PIANO_ROLL_PITCH_HIGHLIGHT = ColourRole::PIANO_ROLL_PITCH_HIGHLIGHT;
    static constexpr auto PIANO_ROLL_GRID_BACKGROUND = ColourRole::PIANO_ROLL_GRID_BACKGROUND;
    static constexpr auto PIANO_ROLL_GRID_BLACK_KEY = ColourRole::PIANO_ROLL_GRID_BLACK_KEY;
    static constexpr auto PIANO_ROLL_GRID_SUBDIVISION = ColourRole::PIANO_ROLL_GRID_SUBDIVISION;
    static constexpr auto PIANO_ROLL_GRID_BEAT = ColourRole::PIANO_ROLL_GRID_BEAT;
    static constexpr auto PIANO_ROLL_GRID_BAR = ColourRole::PIANO_ROLL_GRID_BAR;
    static constexpr auto PIANO_ROLL_CHORD_PREVIEW = ColourRole::PIANO_ROLL_CHORD_PREVIEW;
    static constexpr auto PIANO_ROLL_TOOLTIP_BACKGROUND = ColourRole::PIANO_ROLL_TOOLTIP_BACKGROUND;
    static constexpr auto PIANO_ROLL_FALLBACK_CLIP = ColourRole::PIANO_ROLL_FALLBACK_CLIP;
    static constexpr auto CLIP_BOUNDARY = ColourRole::CLIP_BOUNDARY;
    static constexpr auto PIANO_ROLL_TAKE_LANE_ACTIVE = ColourRole::PIANO_ROLL_TAKE_LANE_ACTIVE;
    static constexpr auto PIANO_ROLL_TAKE_LANE_INACTIVE = ColourRole::PIANO_ROLL_TAKE_LANE_INACTIVE;

    // ========================================================================
    // Shared controls whose original Dark shades have distinct visual roles
    // ========================================================================
    static constexpr auto TEXT_SLIDER_THUMB = ColourRole::TEXT_SLIDER_THUMB;
    static constexpr auto TEXT_SLIDER_METER_LOW = ColourRole::TEXT_SLIDER_METER_LOW;
    static constexpr auto TEXT_SLIDER_METER_WARNING = ColourRole::TEXT_SLIDER_METER_WARNING;
    static constexpr auto TEXT_SLIDER_METER_HIGH = ColourRole::TEXT_SLIDER_METER_HIGH;
    static constexpr auto TOAST_BACKGROUND = ColourRole::TOAST_BACKGROUND;
    static constexpr auto QWERTY_WHITE_KEY_NOTE_TEXT = ColourRole::QWERTY_WHITE_KEY_NOTE_TEXT;
    static constexpr auto EQ_BAND_LOW = ColourRole::EQ_BAND_LOW;
    static constexpr auto EQ_BAND_LOW_MID = ColourRole::EQ_BAND_LOW_MID;
    static constexpr auto EQ_BAND_HIGH_MID = ColourRole::EQ_BAND_HIGH_MID;
    static constexpr auto EQ_BAND_HIGH = ColourRole::EQ_BAND_HIGH;
    static constexpr auto ICON_BACKGROUND = ColourRole::ICON_BACKGROUND;
    static constexpr auto ICON_BRIGHT = ColourRole::ICON_BRIGHT;
    static constexpr auto ICON_SUBTLE = ColourRole::ICON_SUBTLE;
    static constexpr auto ICON_MODULATION = ColourRole::ICON_MODULATION;
    static constexpr auto ICON_BRAND = ColourRole::ICON_BRAND;
    static constexpr auto ICON_POWER = ColourRole::ICON_POWER;
    static constexpr auto MIXER_FADER_THUMB = ColourRole::MIXER_FADER_THUMB;
    static constexpr auto MIXER_KNOB_OUTER = ColourRole::MIXER_KNOB_OUTER;
    static constexpr auto MIXER_KNOB_OUTER_STROKE = ColourRole::MIXER_KNOB_OUTER_STROKE;
    static constexpr auto MIXER_KNOB_INNER = ColourRole::MIXER_KNOB_INNER;
    static constexpr auto MIXER_KNOB_GUIDE = ColourRole::MIXER_KNOB_GUIDE;

    // Runtime palette API. Theme changes are expected to happen on JUCE's
    // message thread, alongside the LookAndFeel refresh they trigger.
    static const Palette& getDarkPalette();
    static const Palette& getActivePalette();
    static void setActivePalette(const Palette& palette);
    static void resetToDarkPalette();

    static const SyntaxPalette& getDarkSyntaxPalette();
    static const SyntaxPalette& getActiveSyntaxPalette();
    static void setActiveSyntaxPalette(const SyntaxPalette& palette);

    // Maps a colour from the active or default-Dark palette to its runtime
    // role. This lets legacy UI code that already supplies a DarkTheme colour
    // retain its intent while resolving the final colour at paint time. Only
    // RGB is considered: callers keep their own alpha value.
    static std::optional<ColourRole> findDarkPaletteRole(juce::Colour colour);

    // Replaces the shared source colours used by bundled SVG controls with
    // their active palette roles. Source colours remain asset implementation
    // details; callers only choose semantic colours in code.
    static void applyToSvgIcon(juce::Drawable& drawable);

    // Apply the theme to JUCE's LookAndFeel
    static void applyToLookAndFeel(juce::LookAndFeel_V4& laf);

    // Get color as JUCE Colour object
    static juce::uint32 getColourValue(ColourRole role);
    static juce::Colour getColour(ColourRole role) {
        return juce::Colour(getColourValue(role));
    }
    static juce::uint32 getSyntaxColourValue(SyntaxColourRole role);
    static juce::Colour getSyntaxColour(SyntaxColourRole role) {
        return juce::Colour(getSyntaxColourValue(role));
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
        return getColour(ACCENT_PRIMARY);
    }
    static juce::Colour getBorderColour() {
        return getColour(BORDER);
    }

  private:
    friend class ThemeManager;
    static Palette activePalette_;
    static SyntaxPalette activeSyntaxPalette_;
};

// Owns theme identity and runtime selection. DarkTheme remains the legacy
// colour-role facade; neutral theme lifecycle APIs live here so Light and
// future JSON themes do not inherit dark-only naming.
class ThemeManager {
  public:
    static constexpr const char* kDarkThemeId = "dark";
    static constexpr const char* kLightThemeId = "light";
    static constexpr const char* kHighContrastThemeId = "high-contrast";

    // Returns false without changing the active theme for an unknown ID.
    static bool setActiveBuiltInTheme(const std::string& themeId);
    static bool isBuiltInTheme(const std::string& themeId);
    static bool isLightTheme();

    // Read-only access to a built-in table by id (dark/light/high-contrast;
    // unknown ids resolve to dark). Lets the JSON theme loader inherit a base
    // palette without disturbing the currently active one.
    static const DarkTheme::Palette& builtInPalette(const std::string& themeId);
    static const DarkTheme::SyntaxPalette& builtInSyntaxPalette(const std::string& themeId);
};

// User colours stay stored verbatim. Only their presentation swatch is
// normalized, with a slightly deeper target on light surfaces so clips and
// headers retain contrast without changing hue.
inline juce::Colour deriveTrackSwatch(juce::Colour stored, float alpha) {
    if (stored.getSaturation() < 0.05f)
        return stored.withAlpha(alpha);

    const float lightness = ThemeManager::isLightTheme() ? 0.48f : 0.55f;
    const float chroma = ThemeManager::isLightTheme() ? 0.12f : 0.10f;
    return oklchToColour(lightness, chroma, sRgbToOklchHue(stored), alpha);
}

// Clip bodies darken the swatch on dark surfaces so the border and the black
// note/waveform ink read against it. The light swatch is already the deeper
// variant, so darkening it again would land near-black on light surfaces.
inline juce::Colour deriveClipBody(juce::Colour stored) {
    const auto swatch = deriveTrackSwatch(stored);
    return ThemeManager::isLightTheme() ? swatch : swatch.darker(0.3f);
}

// A window's background is a per-component colour override, handed over once
// when the window is constructed. JUCE keeps such overrides across a
// look-and-feel change, so unlike a colour read inside paint() it does not
// follow a live theme switch on its own. Nor is it always hidden behind the
// content: the in-window menu bar draws its fill at 40% alpha, and JUCE's own
// dialog chrome leaves margins, so the stale colour shows through.
//
// Call this from the hosted content's lookAndFeelChanged() to re-resolve it.
inline void refreshHostWindowBackground(juce::Component& content,
                                        ColourRole role = ColourRole::PANEL_BACKGROUND) {
    if (auto* window = content.findParentComponentOfClass<juce::ResizableWindow>())
        window->setBackgroundColour(DarkTheme::getColour(role));
}

// Keeps named colours in a custom device UI bound to a role without forcing
// every paint call to spell out DarkTheme::getColour(). The conversion and
// common modifiers resolve the active palette at the point of use.
class ThemedColour {
  public:
    constexpr explicit ThemedColour(ColourRole role) : role_(role) {}

    operator juce::Colour() const {
        return DarkTheme::getColour(role_);
    }
    juce::Colour withAlpha(float alpha) const {
        return DarkTheme::getColour(role_).withAlpha(alpha);
    }
    juce::Colour brighter(float amount = 0.4f) const {
        return DarkTheme::getColour(role_).brighter(amount);
    }
    juce::Colour darker(float amount = 0.4f) const {
        return DarkTheme::getColour(role_).darker(amount);
    }

  private:
    ColourRole role_;
};

}  // namespace magda
