#include "ThemeSerialization.hpp"

#include <array>

namespace magda {

namespace {

// Canonical camelCase names, one per ColourRole, IN ENUM ORDER. The static
// assert below guarantees the count matches; the ordering must track the
// enum exactly (same positional contract as the palette arrays in
// DarkTheme.cpp).
constexpr std::array<const char*, static_cast<std::size_t>(ColourRole::count)> kColourRoleNames{{
    "e0",
    "e1",
    "e2",
    "e3",
    "hairline",
    "background",
    "backgroundAlt",
    "panelBackground",
    "surface",
    "surfaceHover",
    "transportBackground",
    "buttonNormal",
    "buttonHover",
    "buttonPressed",
    "buttonActive",
    "buttonStroke",
    "controlValueFill",
    "controlSliderThumb",
    "textPrimary",
    "textSecondary",
    "textDim",
    "textDisabled",
    // Accents are exposed as a hue-neutral ramp: a theme is free to recolour
    // them, so the JSON keys must not bake in the default hue. The enum keeps
    // its hue names for call-site readability; this table is the mapping.
    "accent1",  // ACCENT_PRIMARY       - primary accent (selection, active)
    "accent6",  // ACCENT_PRIMARY_SOFT - soft variant of accent1
    "accent5",  // ACCENT_INFO       - secondary / info
    "accent3",  // ACCENT_POSITIVE      - positive (waveforms, success)
    "accent2",  // ACCENT_ATTENTION     - attention (tempo, cursors)
    "accent4",  // ACCENT_MODULATION     - modulation (automation)
    "presetIndigo",
    "midiLearn",
    "stepRecord",
    "masterTrackColour",
    "statusSuccess",
    "statusWarning",
    "statusError",
    "statusDanger",
    "trackBackground",
    "trackSelected",
    "trackHeaderSelected",
    "trackHeaderSelectedText",
    "trackSeparator",
    "timelineBackground",
    "gridLine",
    "beatLine",
    "barLine",
    "border",
    "separator",
    "resizeHandle",
    "waveformNormal",
    "waveformSelected",
    "levelMeterGreen",
    "levelMeterYellow",
    "levelMeterRed",
    "gainMeterLow",
    "gainMeterWarning",
    "gainMeterHigh",
    "gateCurve",
    "gateThreshold",
    "multibandLow",
    "multibandMid",
    "multibandHigh",
    "multibandLimit",
    "samplerStartMarker",
    "samplerEndMarker",
    "spectrumOverlay",
    "instrumentBackground",
    "instrumentPanel",
    "instrumentBorder",
    "instrumentText",
    "instrumentTextDim",
    "timeSelection",
    "loopRegion",
    "loopMarker",
    "offsetMarker",
    "textBright",
    "inputBackground",
    "tooltipBackground",
    "iconNeutral",
    "iconTransport",
    "iconOnAccent",
    "automationLaneBackground",
    "automationLaneSelected",
    "automationLaneHeader",
    "automationLaneScaleBackground",
    "automationDivider",
    "automationDividerLight",
    "automationGuide",
    "automationScaleText",
    "automationScaleLabel",
    "automationText",
    "automationPoint",
    "automationPointHover",
    "automationBezier",
    "automationTensionHover",
    "curveBackground",
    "curveTooltipBackground",
    "curveTooltipText",
    "curvePoint",
    "curveHandleBackground",
    "curveHandleNormal",
    "textDark",
    "pianoRollBackground",
    "pianoRollKeyWhite",
    "pianoRollKeyHighlight",
    "pianoRollKeySeparator",
    "pianoRollPitchHighlight",
    "pianoRollGridBackground",
    "pianoRollGridBlackKey",
    "pianoRollGridSubdivision",
    "pianoRollGridBeat",
    "pianoRollGridBar",
    "pianoRollChordPreview",
    "pianoRollTooltipBackground",
    "pianoRollFallbackClip",
    "clipBoundary",
    "pianoRollTakeLaneActive",
    "pianoRollTakeLaneInactive",
    "textSliderThumb",
    "textSliderMeterLow",
    "textSliderMeterWarning",
    "textSliderMeterHigh",
    "toastBackground",
    "qwertyWhiteKeyNoteText",
    "eqBandLow",
    "eqBandLowMid",
    "eqBandHighMid",
    "eqBandHigh",
    "iconBackground",
    "iconBright",
    "iconSubtle",
    "iconModulation",
    "iconBrand",
    "iconPower",
    "mixerFaderThumb",
    "mixerKnobOuter",
    "mixerKnobOuterStroke",
    "mixerKnobInner",
    "mixerKnobGuide",
}};

static_assert(kColourRoleNames.size() == static_cast<std::size_t>(ColourRole::count),
              "kColourRoleNames must have exactly one entry per ColourRole");

constexpr std::array<const char*, static_cast<std::size_t>(SyntaxColourRole::count)>
    kSyntaxColourRoleNames{{
        "editorBackground",     "editorDefaultText",   "lineNumberBackground",
        "lineNumberText",       "editorCaret",         "dslCaret",
        "editorSelection",      "dslSelection",        "dslStatusBackground",
        "dslStatusText",        "dslOutputPrompt",     "dslOutputInfo",
        "dslOutputText",        "dslOutputError",      "dslTokenError",
        "dslTokenComment",      "dslTokenKeyword",     "dslTokenMethod",
        "dslTokenParam",        "dslTokenOperator",    "dslTokenIdentifier",
        "dslTokenNumber",       "dslTokenString",      "dslTokenBracket",
        "dslTokenPunctuation",  "dslTokenNoteName",    "chatTokenText",
        "chatTokenPluginAlias", "chatTokenParamAlias", "chatTokenSlashCommand",
        "chatTokenPunctuation",
    }};

static_assert(kSyntaxColourRoleNames.size() == static_cast<std::size_t>(SyntaxColourRole::count),
              "kSyntaxColourRoleNames must have exactly one entry per SyntaxColourRole");

// Lowercase, dropping every non-alphanumeric character, so callers can spell
// keys in camelCase, snake_case, kebab-case or SCREAMING_CASE interchangeably.
juce::String normalizeKey(const juce::String& key) {
    juce::String out;
    out.preallocateBytes(static_cast<size_t>(key.length()));
    for (auto ch : key)
        if (juce::CharacterFunctions::isLetterOrDigit(ch))
            out += juce::CharacterFunctions::toLowerCase(ch);
    return out;
}

}  // namespace

const char* colourRoleName(ColourRole role) {
    return kColourRoleNames[static_cast<std::size_t>(role)];
}

const char* syntaxColourRoleName(SyntaxColourRole role) {
    return kSyntaxColourRoleNames[static_cast<std::size_t>(role)];
}

std::optional<ColourRole> findColourRole(const juce::String& key) {
    const auto norm = normalizeKey(key);
    if (norm.isEmpty())
        return std::nullopt;
    for (std::size_t i = 0; i < kColourRoleNames.size(); ++i)
        if (normalizeKey(kColourRoleNames[i]) == norm)
            return static_cast<ColourRole>(i);
    return std::nullopt;
}

std::optional<SyntaxColourRole> findSyntaxColourRole(const juce::String& key) {
    const auto norm = normalizeKey(key);
    if (norm.isEmpty())
        return std::nullopt;
    for (std::size_t i = 0; i < kSyntaxColourRoleNames.size(); ++i)
        if (normalizeKey(kSyntaxColourRoleNames[i]) == norm)
            return static_cast<SyntaxColourRole>(i);
    return std::nullopt;
}

std::optional<juce::Colour> parseColourString(const juce::String& text) {
    juce::String s = text.trim();
    if (s.startsWithChar('#'))
        s = s.substring(1);
    else if (s.startsWithIgnoreCase("0x"))
        s = s.substring(2);

    if (s.length() != 6 && s.length() != 8)
        return std::nullopt;

    if (!s.containsOnly("0123456789abcdefABCDEF"))
        return std::nullopt;

    const auto value = static_cast<juce::uint32>(s.getHexValue64());
    if (s.length() == 6)
        return juce::Colour(0xFF000000u | value);  // opaque RRGGBB
    return juce::Colour(value);                    // AARRGGBB
}

juce::String colourToHexString(juce::Colour colour) {
    // int64 avoids the sign issues that arise from casting a >= 0x80000000
    // ARGB word to a 32-bit int.
    const auto argb = static_cast<juce::int64>(colour.getARGB());
    if (colour.getAlpha() == 0xFF)
        return "#" + juce::String::toHexString(argb & 0x00FFFFFF).paddedLeft('0', 6).toUpperCase();
    return "#" + juce::String::toHexString(argb).paddedLeft('0', 8).toUpperCase();
}

}  // namespace magda
