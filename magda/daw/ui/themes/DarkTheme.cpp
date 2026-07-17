#include "DarkTheme.hpp"

namespace magda {

namespace {

constexpr DarkTheme::Palette darkPalette{
    // Elevation ramp
    0xFF0C0F14,  // E0
    0xFF151A21,  // E1
    0xFF1E242D,  // E2
    0xFF28303A,  // E3
    0xFF2C343E,  // HAIRLINE

    // Backgrounds
    0xFF0C0F14,  // BACKGROUND
    0xFF151A21,  // BACKGROUND_ALT
    0xFF151A21,  // PANEL_BACKGROUND
    0xFF1E242D,  // SURFACE
    0xFF28303A,  // SURFACE_HOVER

    // Transport and controls
    0xFF151A21,  // TRANSPORT_BACKGROUND
    0xFF0C0F14,  // BUTTON_NORMAL
    0xFF1E242D,  // BUTTON_HOVER
    0xFF28303A,  // BUTTON_PRESSED
    0xFF5588AA,  // BUTTON_ACTIVE
    0xFF2C343E,  // BUTTON_STROKE
    0x12E8EDF1,  // CONTROL_VALUE_FILL
    0xFF999999,  // CONTROL_SLIDER_THUMB

    // Text
    0xFFE8EDF1,  // TEXT_PRIMARY
    0xFFAEB6BD,  // TEXT_SECONDARY
    0xFF7C858D,  // TEXT_DIM
    0xFF666666,  // TEXT_DISABLED

    // Accents
    0xFF5588AA,  // ACCENT_BLUE
    0xFF88AACC,  // ACCENT_BLUE_LIGHT
    0xFF66AAFF,  // ACCENT_CYAN
    0xFF43C07A,  // ACCENT_GREEN
    0xFFFF8822,  // ACCENT_ORANGE
    0xFF7777DD,  // ACCENT_PURPLE
    0xFF5577CC,  // PRESET_INDIGO
    0xFFFF6B35,  // MIDI_LEARN
    0xFFCC3333,  // STEP_RECORD
    0xFF6655AA,  // MASTER_TRACK_COLOUR

    // Status
    0xFF44AA44,  // STATUS_SUCCESS
    0xFFFFAA44,  // STATUS_WARNING
    0xFFAA4444,  // STATUS_ERROR
    0xFFE64343,  // STATUS_DANGER

    // Tracks
    0xFF151A21,  // TRACK_BACKGROUND
    0xFF1E242D,  // TRACK_SELECTED
    0xFFB4BCC6,  // TRACK_HEADER_SELECTED
    0xFF0C0F14,  // TRACK_HEADER_SELECTED_TEXT
    0xFF0C0F14,  // TRACK_SEPARATOR

    // Timeline and grid
    0xFF151A21,  // TIMELINE_BACKGROUND
    0xFF383840,  // GRID_LINE
    0xFF484850,  // BEAT_LINE
    0xFF555555,  // BAR_LINE

    // Borders and separators
    0xFF2C343E,  // BORDER
    0xFF2C343E,  // SEPARATOR
    0xFF3A4450,  // RESIZE_HANDLE

    // Audio visualization
    0xFF33E680,  // WAVEFORM_NORMAL
    0xFF66AAFF,  // WAVEFORM_SELECTED
    0xFF44AA44,  // LEVEL_METER_GREEN
    0xFFFFAA44,  // LEVEL_METER_YELLOW
    0xFFAA4444,  // LEVEL_METER_RED
    0xFF2ECC71,  // GAIN_METER_LOW
    0xFFF39C12,  // GAIN_METER_WARNING
    0xFFE74C3C,  // GAIN_METER_HIGH
    0xFF00D4FF,  // GATE_CURVE
    0xFFFF8C00,  // GATE_THRESHOLD
    0xFF43A0FF,  // MULTIBAND_LOW
    0xFFFFB347,  // MULTIBAND_MID
    0xFFAA66FF,  // MULTIBAND_HIGH
    0xFFFF0000,  // MULTIBAND_LIMIT
    0xFFFF9800,  // SAMPLER_START_MARKER
    0xFFE53935,  // SAMPLER_END_MARKER
    0xFFC8C8C8,  // SPECTRUM_OVERLAY
    0xFF0D0D0F,  // INSTRUMENT_BACKGROUND
    0xFF141417,  // INSTRUMENT_PANEL
    0xFF242428,  // INSTRUMENT_BORDER
    0xFFE4E4E8,  // INSTRUMENT_TEXT
    0xFF7A7A84,  // INSTRUMENT_TEXT_DIM

    // Selection and loop regions
    0x335588AA,  // TIME_SELECTION
    0x08FFFFFF,  // LOOP_REGION
    0xFF44AA66,  // LOOP_MARKER
    0xFFCCAA44,  // OFFSET_MARKER

    // Shared UI and automation editor
    0xFFFFFFFF,  // TEXT_BRIGHT
    0xFF111111,  // INPUT_BACKGROUND
    0xFF161616,  // TOOLTIP_BACKGROUND
    0xFFB3B3B3,  // ICON_NEUTRAL
    0xFFBCBCBC,  // ICON_TRANSPORT
    0xFF1E1E1E,  // ICON_ON_ACCENT
    0xFF1E1E1E,  // AUTOMATION_LANE_BACKGROUND
    0xFF2A2A2A,  // AUTOMATION_LANE_SELECTED
    0xFF252525,  // AUTOMATION_LANE_HEADER
    0xFF1A1A1A,  // AUTOMATION_LANE_SCALE_BACKGROUND
    0xFF333333,  // AUTOMATION_DIVIDER
    0xFF444444,  // AUTOMATION_DIVIDER_LIGHT
    0xFF3A3A3A,  // AUTOMATION_GUIDE
    0xFF888888,  // AUTOMATION_SCALE_TEXT
    0xFF777777,  // AUTOMATION_SCALE_LABEL
    0xFFCCCCCC,  // AUTOMATION_TEXT
    0xFFAAAAAA,  // AUTOMATION_POINT
    0xFFCCCCCC,  // AUTOMATION_POINT_HOVER
    0xFF6688CC,  // AUTOMATION_BEZIER
    0xFFCCAA88,  // AUTOMATION_TENSION_HOVER
    0xFF1A1A1A,  // CURVE_BACKGROUND
    0xDD222222,  // CURVE_TOOLTIP_BACKGROUND
    0xFFEEEEEE,  // CURVE_TOOLTIP_TEXT
    0xFFFF8A2A,  // CURVE_POINT
    0xFF141414,  // CURVE_HANDLE_BACKGROUND
    0xFFE6E6E6,  // CURVE_HANDLE_NORMAL

    // Piano roll
    0xFF000000,  // TEXT_DARK
    0xFF1A1A1A,  // PIANO_ROLL_BACKGROUND
    0xFFE8E8E8,  // PIANO_ROLL_KEY_WHITE
    0xFF4A9EFF,  // PIANO_ROLL_KEY_HIGHLIGHT
    0xFFCCCCCC,  // PIANO_ROLL_KEY_SEPARATOR
    0xFF6688CC,  // PIANO_ROLL_PITCH_HIGHLIGHT
    0xFF3A3A3A,  // PIANO_ROLL_GRID_BACKGROUND
    0xFF2A2A2A,  // PIANO_ROLL_GRID_BLACK_KEY
    0xFF505050,  // PIANO_ROLL_GRID_SUBDIVISION
    0xFF585858,  // PIANO_ROLL_GRID_BEAT
    0xFF707070,  // PIANO_ROLL_GRID_BAR
    0xFF5599FF,  // PIANO_ROLL_CHORD_PREVIEW
    0xFF202020,  // PIANO_ROLL_TOOLTIP_BACKGROUND
    0xFF808080,  // PIANO_ROLL_FALLBACK_CLIP
    0xFF6A7280,  // CLIP_BOUNDARY
    0xFF333333,  // PIANO_ROLL_TAKE_LANE_ACTIVE
    0xFF262626,  // PIANO_ROLL_TAKE_LANE_INACTIVE

    // Shared controls
    0xFFBCD4E8,  // TEXT_SLIDER_THUMB
    0xFF4CAF50,  // TEXT_SLIDER_METER_LOW
    0xFFFFC107,  // TEXT_SLIDER_METER_WARNING
    0xFFF44336,  // TEXT_SLIDER_METER_HIGH
    0xFF222233,  // TOAST_BACKGROUND
    0xFF1F1F1F,  // QWERTY_WHITE_KEY_NOTE_TEXT
    0xFFE06C75,  // EQ_BAND_LOW
    0xFF61AFEF,  // EQ_BAND_LOW_MID
    0xFF98C379,  // EQ_BAND_HIGH_MID
    0xFFE5C07B,  // EQ_BAND_HIGH
    0xFF2E2E33,  // ICON_BACKGROUND
    0xFFE3E3E3,  // ICON_BRIGHT
    0xFF808080,  // ICON_SUBTLE
    0xFF979797,  // ICON_MODULATION
    0xFFD9D9D9,  // ICON_BRAND
    0xFFE6E6E6,  // ICON_POWER
    0xFF2A3A4A,  // MIXER_FADER_THUMB
    0xFF2A2A35,  // MIXER_KNOB_OUTER
    0xFF3A3A45,  // MIXER_KNOB_OUTER_STROKE
    0xFF1E1E22,  // MIXER_KNOB_INNER
    0xFF404050,  // MIXER_KNOB_GUIDE
};

constexpr DarkTheme::SyntaxPalette darkSyntaxPalette{
    0xFF0C0F14,  // EDITOR_BACKGROUND
    0xFFE8EDF1,  // EDITOR_DEFAULT_TEXT
    0xFF252526,  // LINE_NUMBER_BACKGROUND
    0xFF858585,  // LINE_NUMBER_TEXT
    0xFFE8EDF1,  // EDITOR_CARET
    0xFF88FF88,  // DSL_CARET
    0x4D5588AA,  // EDITOR_SELECTION
    0xFF264F78,  // DSL_SELECTION
    0xFF007ACC,  // DSL_STATUS_BACKGROUND
    0xFFFFFFFF,  // DSL_STATUS_TEXT
    0xFF88FF88,  // DSL_OUTPUT_PROMPT
    0xFF569CD6,  // DSL_OUTPUT_INFO
    0xFFD4D4D4,  // DSL_OUTPUT_TEXT
    0xFFF48771,  // DSL_OUTPUT_ERROR
    0xFFCC0000,  // DSL_TOKEN_ERROR
    0xFF6A9955,  // DSL_TOKEN_COMMENT
    0xFF569CD6,  // DSL_TOKEN_KEYWORD
    0xFFDCDCAA,  // DSL_TOKEN_METHOD
    0xFF9CDCFE,  // DSL_TOKEN_PARAM
    0xFFD4D4D4,  // DSL_TOKEN_OPERATOR
    0xFFD4D4D4,  // DSL_TOKEN_IDENTIFIER
    0xFFB5CEA8,  // DSL_TOKEN_NUMBER
    0xFFCE9178,  // DSL_TOKEN_STRING
    0xFFD4D4D4,  // DSL_TOKEN_BRACKET
    0xFFD4D4D4,  // DSL_TOKEN_PUNCTUATION
    0xFF4EC9B0,  // DSL_TOKEN_NOTE_NAME
    0xFFE0E0E0,  // CHAT_TOKEN_TEXT
    0xFF5FA8FF,  // CHAT_TOKEN_PLUGIN_ALIAS
    0xFFE0A85A,  // CHAT_TOKEN_PARAM_ALIAS
    0xFF7ACF68,  // CHAT_TOKEN_SLASH_COMMAND
    0xFFD4D4D4,  // CHAT_TOKEN_PUNCTUATION
};

constexpr std::size_t colourRoleIndex(ColourRole role) {
    return static_cast<std::size_t>(role);
}

constexpr std::size_t syntaxColourRoleIndex(SyntaxColourRole role) {
    return static_cast<std::size_t>(role);
}

constexpr DarkTheme::Palette lightPalette = [] {
    auto palette = darkPalette;
    const auto set = [&palette](ColourRole role, juce::uint32 colour) {
        palette[colourRoleIndex(role)] = colour;
    };

    // Elevation and application surfaces
    set(ColourRole::E0, 0xFFF4F6F8);
    set(ColourRole::E1, 0xFFECF0F3);
    set(ColourRole::E2, 0xFFE3E8EC);
    set(ColourRole::E3, 0xFFD8DEE4);
    set(ColourRole::HAIRLINE, 0xFFC2CAD2);
    set(ColourRole::BACKGROUND, 0xFFF4F6F8);
    set(ColourRole::BACKGROUND_ALT, 0xFFECF0F3);
    set(ColourRole::PANEL_BACKGROUND, 0xFFECF0F3);
    set(ColourRole::SURFACE, 0xFFE3E8EC);
    set(ColourRole::SURFACE_HOVER, 0xFFD8DEE4);

    // Transport, controls, and text
    set(ColourRole::TRANSPORT_BACKGROUND, 0xFFECF0F3);
    set(ColourRole::BUTTON_NORMAL, 0xFFF7F9FA);
    set(ColourRole::BUTTON_HOVER, 0xFFE3E8EC);
    set(ColourRole::BUTTON_PRESSED, 0xFFD2D9E0);
    set(ColourRole::BUTTON_ACTIVE, 0xFF2E668C);
    set(ColourRole::BUTTON_STROKE, 0xFFB8C2CB);
    set(ColourRole::CONTROL_VALUE_FILL, 0x242E668C);
    set(ColourRole::CONTROL_SLIDER_THUMB, 0xFF52606C);
    set(ColourRole::TEXT_PRIMARY, 0xFF1B242C);
    set(ColourRole::TEXT_SECONDARY, 0xFF52606C);
    set(ColourRole::TEXT_DIM, 0xFF71808C);
    set(ColourRole::TEXT_DISABLED, 0xFF9AA4AD);

    // Accents and status colours are deliberately deeper than Dark's so they
    // meet light-surface contrast without changing their semantic hue.
    set(ColourRole::ACCENT_BLUE, 0xFF2E668C);
    set(ColourRole::ACCENT_BLUE_LIGHT, 0xFF4E7897);
    set(ColourRole::ACCENT_CYAN, 0xFF087B8C);
    set(ColourRole::ACCENT_GREEN, 0xFF197A4B);
    set(ColourRole::ACCENT_ORANGE, 0xFFC45A00);
    set(ColourRole::ACCENT_PURPLE, 0xFF6250B5);
    set(ColourRole::PRESET_INDIGO, 0xFF405EA8);
    set(ColourRole::MIDI_LEARN, 0xFFC94D1A);
    set(ColourRole::STEP_RECORD, 0xFFB82929);
    set(ColourRole::MASTER_TRACK_COLOUR, 0xFF57469A);
    set(ColourRole::STATUS_SUCCESS, 0xFF237A36);
    set(ColourRole::STATUS_WARNING, 0xFF9B6500);
    set(ColourRole::STATUS_ERROR, 0xFFA93636);
    set(ColourRole::STATUS_DANGER, 0xFFC52E2E);

    // Tracks, timeline, grid, and separators
    set(ColourRole::TRACK_BACKGROUND, 0xFFECF0F3);
    set(ColourRole::TRACK_SELECTED, 0xFFDCE7EE);
    set(ColourRole::TRACK_HEADER_SELECTED, 0xFF304B5E);
    set(ColourRole::TRACK_HEADER_SELECTED_TEXT, 0xFFFFFFFF);
    set(ColourRole::TRACK_SEPARATOR, 0xFFC4CCD3);
    set(ColourRole::TIMELINE_BACKGROUND, 0xFFE9EDF0);
    set(ColourRole::GRID_LINE, 0xFFD1D7DC);
    set(ColourRole::BEAT_LINE, 0xFFB9C2CA);
    set(ColourRole::BAR_LINE, 0xFF929EA8);
    set(ColourRole::BORDER, 0xFFB8C2CB);
    set(ColourRole::SEPARATOR, 0xFFC2CAD2);
    set(ColourRole::RESIZE_HANDLE, 0xFF98A5AF);

    // Waveforms, meters, analyzers, and device visualizations
    set(ColourRole::WAVEFORM_NORMAL, 0xFF087A43);
    set(ColourRole::WAVEFORM_SELECTED, 0xFF185FA8);
    set(ColourRole::LEVEL_METER_GREEN, 0xFF237A36);
    set(ColourRole::LEVEL_METER_YELLOW, 0xFF9B6500);
    set(ColourRole::LEVEL_METER_RED, 0xFFA93636);
    set(ColourRole::GAIN_METER_LOW, 0xFF168044);
    set(ColourRole::GAIN_METER_WARNING, 0xFFB36B00);
    set(ColourRole::GAIN_METER_HIGH, 0xFFC0392B);
    set(ColourRole::GATE_CURVE, 0xFF00758A);
    set(ColourRole::GATE_THRESHOLD, 0xFFB85F00);
    set(ColourRole::MULTIBAND_LOW, 0xFF1E64B0);
    set(ColourRole::MULTIBAND_MID, 0xFF9B6500);
    set(ColourRole::MULTIBAND_HIGH, 0xFF7848B5);
    set(ColourRole::MULTIBAND_LIMIT, 0xFFC52E2E);
    set(ColourRole::SAMPLER_START_MARKER, 0xFFB85F00);
    set(ColourRole::SAMPLER_END_MARKER, 0xFFBE2F2B);
    set(ColourRole::SPECTRUM_OVERLAY, 0xFF4D5963);
    set(ColourRole::INSTRUMENT_BACKGROUND, 0xFFF2F4F6);
    set(ColourRole::INSTRUMENT_PANEL, 0xFFE7EBEF);
    set(ColourRole::INSTRUMENT_BORDER, 0xFFB7C0C8);
    set(ColourRole::INSTRUMENT_TEXT, 0xFF202830);
    set(ColourRole::INSTRUMENT_TEXT_DIM, 0xFF687681);

    // Regions, shared UI, and automation editor
    set(ColourRole::TIME_SELECTION, 0x4D2E668C);
    set(ColourRole::LOOP_REGION, 0x10000000);
    set(ColourRole::LOOP_MARKER, 0xFF287441);
    set(ColourRole::OFFSET_MARKER, 0xFF8D681C);
    set(ColourRole::TEXT_BRIGHT, 0xFFFFFFFF);
    set(ColourRole::INPUT_BACKGROUND, 0xFFFFFFFF);
    set(ColourRole::TOOLTIP_BACKGROUND, 0xEE26313A);
    set(ColourRole::ICON_NEUTRAL, 0xFF46535E);
    set(ColourRole::ICON_TRANSPORT, 0xFF35434E);
    set(ColourRole::ICON_ON_ACCENT, 0xFFFFFFFF);
    set(ColourRole::AUTOMATION_LANE_BACKGROUND, 0xFFF0F2F4);
    set(ColourRole::AUTOMATION_LANE_SELECTED, 0xFFDDE7EE);
    set(ColourRole::AUTOMATION_LANE_HEADER, 0xFFE5E9ED);
    set(ColourRole::AUTOMATION_LANE_SCALE_BACKGROUND, 0xFFE9EDF0);
    set(ColourRole::AUTOMATION_DIVIDER, 0xFFB4BDC5);
    set(ColourRole::AUTOMATION_DIVIDER_LIGHT, 0xFF929EA8);
    set(ColourRole::AUTOMATION_GUIDE, 0xFFA6B0B8);
    set(ColourRole::AUTOMATION_SCALE_TEXT, 0xFF53616C);
    set(ColourRole::AUTOMATION_SCALE_LABEL, 0xFF687681);
    set(ColourRole::AUTOMATION_TEXT, 0xFF26313A);
    set(ColourRole::AUTOMATION_POINT, 0xFF52606C);
    set(ColourRole::AUTOMATION_POINT_HOVER, 0xFF1B242C);
    set(ColourRole::AUTOMATION_BEZIER, 0xFF356BA0);
    set(ColourRole::AUTOMATION_TENSION_HOVER, 0xFF9A5A20);
    set(ColourRole::CURVE_BACKGROUND, 0xFFF5F6F7);
    set(ColourRole::CURVE_TOOLTIP_BACKGROUND, 0xEE26313A);
    set(ColourRole::CURVE_TOOLTIP_TEXT, 0xFFFFFFFF);
    set(ColourRole::CURVE_POINT, 0xFFC45A00);
    set(ColourRole::CURVE_HANDLE_BACKGROUND, 0xFFE2E7EB);
    set(ColourRole::CURVE_HANDLE_NORMAL, 0xFF35434E);

    // Piano roll and take lanes
    set(ColourRole::TEXT_DARK, 0xFF111820);
    set(ColourRole::PIANO_ROLL_BACKGROUND, 0xFFF1F3F5);
    set(ColourRole::PIANO_ROLL_KEY_WHITE, 0xFFFAFBFC);
    set(ColourRole::PIANO_ROLL_KEY_HIGHLIGHT, 0xFF3477B8);
    set(ColourRole::PIANO_ROLL_KEY_SEPARATOR, 0xFFAFB8C0);
    set(ColourRole::PIANO_ROLL_PITCH_HIGHLIGHT, 0xFF4D78A4);
    set(ColourRole::PIANO_ROLL_GRID_BACKGROUND, 0xFFE7EAED);
    set(ColourRole::PIANO_ROLL_GRID_BLACK_KEY, 0xFFD9DEE3);
    set(ColourRole::PIANO_ROLL_GRID_SUBDIVISION, 0xFFC9D0D6);
    set(ColourRole::PIANO_ROLL_GRID_BEAT, 0xFFABB5BD);
    set(ColourRole::PIANO_ROLL_GRID_BAR, 0xFF87949E);
    set(ColourRole::PIANO_ROLL_CHORD_PREVIEW, 0xFF2469A8);
    set(ColourRole::PIANO_ROLL_TOOLTIP_BACKGROUND, 0xEE26313A);
    set(ColourRole::PIANO_ROLL_FALLBACK_CLIP, 0xFF667681);
    set(ColourRole::CLIP_BOUNDARY, 0xFF536778);
    set(ColourRole::PIANO_ROLL_TAKE_LANE_ACTIVE, 0xFFDDE4E9);
    set(ColourRole::PIANO_ROLL_TAKE_LANE_INACTIVE, 0xFFEDF0F2);

    // Shared controls and mixer-specific rendering
    set(ColourRole::TEXT_SLIDER_THUMB, 0xFF34536B);
    set(ColourRole::TEXT_SLIDER_METER_LOW, 0xFF237A36);
    set(ColourRole::TEXT_SLIDER_METER_WARNING, 0xFF9B6500);
    set(ColourRole::TEXT_SLIDER_METER_HIGH, 0xFFB8322B);
    set(ColourRole::TOAST_BACKGROUND, 0xEE26313A);
    set(ColourRole::QWERTY_WHITE_KEY_NOTE_TEXT, 0xFF202830);
    set(ColourRole::EQ_BAND_LOW, 0xFFB33C46);
    set(ColourRole::EQ_BAND_LOW_MID, 0xFF276DA1);
    set(ColourRole::EQ_BAND_HIGH_MID, 0xFF4F7D38);
    set(ColourRole::EQ_BAND_HIGH, 0xFF98701F);
    set(ColourRole::ICON_BACKGROUND, 0xFFE0E5E9);
    set(ColourRole::ICON_BRIGHT, 0xFF27343E);
    set(ColourRole::ICON_SUBTLE, 0xFF71808C);
    set(ColourRole::ICON_MODULATION, 0xFF64727D);
    set(ColourRole::ICON_BRAND, 0xFF26313A);
    set(ColourRole::ICON_POWER, 0xFF35434E);
    set(ColourRole::MIXER_FADER_THUMB, 0xFF7890A3);
    set(ColourRole::MIXER_KNOB_OUTER, 0xFFD4DAE0);
    set(ColourRole::MIXER_KNOB_OUTER_STROKE, 0xFF9EABB5);
    set(ColourRole::MIXER_KNOB_INNER, 0xFFEEF1F3);
    set(ColourRole::MIXER_KNOB_GUIDE, 0xFF8997A2);

    return palette;
}();

constexpr DarkTheme::SyntaxPalette lightSyntaxPalette{
    0xFFF7F8FA,  // EDITOR_BACKGROUND
    0xFF202830,  // EDITOR_DEFAULT_TEXT
    0xFFEAEDF0,  // LINE_NUMBER_BACKGROUND
    0xFF687681,  // LINE_NUMBER_TEXT
    0xFF111820,  // EDITOR_CARET
    0xFF176B34,  // DSL_CARET
    0x4D3477B8,  // EDITOR_SELECTION
    0xFFBED9F2,  // DSL_SELECTION
    0xFF2469A8,  // DSL_STATUS_BACKGROUND
    0xFFFFFFFF,  // DSL_STATUS_TEXT
    0xFF176B34,  // DSL_OUTPUT_PROMPT
    0xFF1C659B,  // DSL_OUTPUT_INFO
    0xFF26313A,  // DSL_OUTPUT_TEXT
    0xFFB8322B,  // DSL_OUTPUT_ERROR
    0xFFB52222,  // DSL_TOKEN_ERROR
    0xFF4F762F,  // DSL_TOKEN_COMMENT
    0xFF1D5F91,  // DSL_TOKEN_KEYWORD
    0xFF765F00,  // DSL_TOKEN_METHOD
    0xFF27618B,  // DSL_TOKEN_PARAM
    0xFF35434E,  // DSL_TOKEN_OPERATOR
    0xFF202830,  // DSL_TOKEN_IDENTIFIER
    0xFF466B2C,  // DSL_TOKEN_NUMBER
    0xFF9B4B28,  // DSL_TOKEN_STRING
    0xFF35434E,  // DSL_TOKEN_BRACKET
    0xFF35434E,  // DSL_TOKEN_PUNCTUATION
    0xFF08725F,  // DSL_TOKEN_NOTE_NAME
    0xFF26313A,  // CHAT_TOKEN_TEXT
    0xFF1C659B,  // CHAT_TOKEN_PLUGIN_ALIAS
    0xFF9A5A20,  // CHAT_TOKEN_PARAM_ALIAS
    0xFF34792F,  // CHAT_TOKEN_SLASH_COMMAND
    0xFF35434E,  // CHAT_TOKEN_PUNCTUATION
};

constexpr DarkTheme::Palette highContrastPalette = [] {
    auto palette = darkPalette;

    // This deliberately pragmatic palette proves the live-selection path.
    // It is not MAGDA's future Light theme, which will get its own contrast
    // and derived-colour pass in issue #89.
    palette[colourRoleIndex(ColourRole::E0)] = 0xFF000000;
    palette[colourRoleIndex(ColourRole::E1)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::E2)] = 0xFF1B1B1B;
    palette[colourRoleIndex(ColourRole::E3)] = 0xFF303030;
    palette[colourRoleIndex(ColourRole::HAIRLINE)] = 0xFF707070;

    palette[colourRoleIndex(ColourRole::BACKGROUND)] = 0xFF000000;
    palette[colourRoleIndex(ColourRole::BACKGROUND_ALT)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::PANEL_BACKGROUND)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::SURFACE)] = 0xFF1B1B1B;
    palette[colourRoleIndex(ColourRole::SURFACE_HOVER)] = 0xFF303030;
    palette[colourRoleIndex(ColourRole::TRANSPORT_BACKGROUND)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::BUTTON_NORMAL)] = 0xFF000000;
    palette[colourRoleIndex(ColourRole::BUTTON_HOVER)] = 0xFF1B1B1B;
    palette[colourRoleIndex(ColourRole::BUTTON_PRESSED)] = 0xFF303030;
    palette[colourRoleIndex(ColourRole::BUTTON_ACTIVE)] = 0xFF4DA3FF;
    palette[colourRoleIndex(ColourRole::BUTTON_STROKE)] = 0xFF707070;
    palette[colourRoleIndex(ColourRole::TEXT_PRIMARY)] = 0xFFFFFFFF;
    palette[colourRoleIndex(ColourRole::TEXT_SECONDARY)] = 0xFFE0E0E0;
    palette[colourRoleIndex(ColourRole::TEXT_DIM)] = 0xFFB0B0B0;
    palette[colourRoleIndex(ColourRole::TEXT_DISABLED)] = 0xFF808080;
    palette[colourRoleIndex(ColourRole::ACCENT_BLUE)] = 0xFF4DA3FF;
    palette[colourRoleIndex(ColourRole::ACCENT_BLUE_LIGHT)] = 0xFF9DCCFF;
    palette[colourRoleIndex(ColourRole::ACCENT_CYAN)] = 0xFF54C7FF;
    palette[colourRoleIndex(ColourRole::ACCENT_GREEN)] = 0xFF54D68B;
    palette[colourRoleIndex(ColourRole::PRESET_INDIGO)] = 0xFF88B7FF;
    palette[colourRoleIndex(ColourRole::MIDI_LEARN)] = 0xFFFF9A73;
    palette[colourRoleIndex(ColourRole::STEP_RECORD)] = 0xFFFF6B6B;
    palette[colourRoleIndex(ColourRole::TRACK_HEADER_SELECTED)] = 0xFFF0F0F0;
    palette[colourRoleIndex(ColourRole::TRACK_HEADER_SELECTED_TEXT)] = 0xFF000000;
    palette[colourRoleIndex(ColourRole::TRACK_SEPARATOR)] = 0xFF000000;
    palette[colourRoleIndex(ColourRole::TIMELINE_BACKGROUND)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::GRID_LINE)] = 0xFF4A4A4A;
    palette[colourRoleIndex(ColourRole::BEAT_LINE)] = 0xFF656565;
    palette[colourRoleIndex(ColourRole::BAR_LINE)] = 0xFF909090;
    palette[colourRoleIndex(ColourRole::BORDER)] = 0xFF707070;
    palette[colourRoleIndex(ColourRole::SEPARATOR)] = 0xFF707070;
    palette[colourRoleIndex(ColourRole::RESIZE_HANDLE)] = 0xFF909090;
    palette[colourRoleIndex(ColourRole::GAIN_METER_LOW)] = 0xFF54D68B;
    palette[colourRoleIndex(ColourRole::GAIN_METER_WARNING)] = 0xFFFFD166;
    palette[colourRoleIndex(ColourRole::GAIN_METER_HIGH)] = 0xFFFF6B6B;
    palette[colourRoleIndex(ColourRole::GATE_CURVE)] = 0xFF54DFFF;
    palette[colourRoleIndex(ColourRole::GATE_THRESHOLD)] = 0xFFFFC15A;
    palette[colourRoleIndex(ColourRole::MULTIBAND_LOW)] = 0xFF76B7FF;
    palette[colourRoleIndex(ColourRole::MULTIBAND_MID)] = 0xFFFFD166;
    palette[colourRoleIndex(ColourRole::MULTIBAND_HIGH)] = 0xFFCD9BFF;
    palette[colourRoleIndex(ColourRole::MULTIBAND_LIMIT)] = 0xFFFF6B6B;
    palette[colourRoleIndex(ColourRole::SAMPLER_START_MARKER)] = 0xFFFFB452;
    palette[colourRoleIndex(ColourRole::SAMPLER_END_MARKER)] = 0xFFFF7171;
    palette[colourRoleIndex(ColourRole::SPECTRUM_OVERLAY)] = 0xFFD8D8D8;
    palette[colourRoleIndex(ColourRole::INSTRUMENT_BACKGROUND)] = 0xFF08080A;
    palette[colourRoleIndex(ColourRole::INSTRUMENT_PANEL)] = 0xFF15151A;
    palette[colourRoleIndex(ColourRole::INSTRUMENT_BORDER)] = 0xFF66666E;
    palette[colourRoleIndex(ColourRole::INSTRUMENT_TEXT)] = 0xFFF3F3F8;
    palette[colourRoleIndex(ColourRole::INSTRUMENT_TEXT_DIM)] = 0xFFB8B8C0;
    palette[colourRoleIndex(ColourRole::TEXT_BRIGHT)] = 0xFFFFFFFF;
    palette[colourRoleIndex(ColourRole::INPUT_BACKGROUND)] = 0xFF080808;
    palette[colourRoleIndex(ColourRole::TOOLTIP_BACKGROUND)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::ICON_NEUTRAL)] = 0xFFD0D0D0;
    palette[colourRoleIndex(ColourRole::ICON_TRANSPORT)] = 0xFFD8D8D8;
    palette[colourRoleIndex(ColourRole::ICON_ON_ACCENT)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::AUTOMATION_LANE_BACKGROUND)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::AUTOMATION_LANE_SELECTED)] = 0xFF202020;
    palette[colourRoleIndex(ColourRole::AUTOMATION_LANE_HEADER)] = 0xFF181818;
    palette[colourRoleIndex(ColourRole::AUTOMATION_LANE_SCALE_BACKGROUND)] = 0xFF080808;
    palette[colourRoleIndex(ColourRole::AUTOMATION_DIVIDER)] = 0xFF707070;
    palette[colourRoleIndex(ColourRole::AUTOMATION_DIVIDER_LIGHT)] = 0xFF909090;
    palette[colourRoleIndex(ColourRole::AUTOMATION_GUIDE)] = 0xFF808080;
    palette[colourRoleIndex(ColourRole::AUTOMATION_SCALE_TEXT)] = 0xFFC0C0C0;
    palette[colourRoleIndex(ColourRole::AUTOMATION_SCALE_LABEL)] = 0xFFB0B0B0;
    palette[colourRoleIndex(ColourRole::AUTOMATION_TEXT)] = 0xFFE0E0E0;
    palette[colourRoleIndex(ColourRole::AUTOMATION_POINT)] = 0xFFC0C0C0;
    palette[colourRoleIndex(ColourRole::AUTOMATION_POINT_HOVER)] = 0xFFE0E0E0;
    palette[colourRoleIndex(ColourRole::AUTOMATION_BEZIER)] = 0xFF7AB8FF;
    palette[colourRoleIndex(ColourRole::AUTOMATION_TENSION_HOVER)] = 0xFFFFCC88;
    palette[colourRoleIndex(ColourRole::CURVE_BACKGROUND)] = 0xFF080808;
    palette[colourRoleIndex(ColourRole::CURVE_TOOLTIP_BACKGROUND)] = 0xEE181818;
    palette[colourRoleIndex(ColourRole::CURVE_TOOLTIP_TEXT)] = 0xFFFFFFFF;
    palette[colourRoleIndex(ColourRole::CURVE_POINT)] = 0xFFFFA24A;
    palette[colourRoleIndex(ColourRole::CURVE_HANDLE_BACKGROUND)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::CURVE_HANDLE_NORMAL)] = 0xFFE0E0E0;
    palette[colourRoleIndex(ColourRole::TEXT_DARK)] = 0xFF000000;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_BACKGROUND)] = 0xFF080808;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_KEY_WHITE)] = 0xFFF0F0F0;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_KEY_HIGHLIGHT)] = 0xFF4DA3FF;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_KEY_SEPARATOR)] = 0xFF909090;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_PITCH_HIGHLIGHT)] = 0xFF7AB8FF;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_GRID_BACKGROUND)] = 0xFF202020;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_GRID_BLACK_KEY)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_GRID_SUBDIVISION)] = 0xFF707070;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_GRID_BEAT)] = 0xFF909090;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_GRID_BAR)] = 0xFFB0B0B0;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_CHORD_PREVIEW)] = 0xFF4DA3FF;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_TOOLTIP_BACKGROUND)] = 0xFF181818;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_FALLBACK_CLIP)] = 0xFFA0A0A0;
    palette[colourRoleIndex(ColourRole::CLIP_BOUNDARY)] = 0xFFB0B0B0;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_TAKE_LANE_ACTIVE)] = 0xFF303030;
    palette[colourRoleIndex(ColourRole::PIANO_ROLL_TAKE_LANE_INACTIVE)] = 0xFF181818;
    palette[colourRoleIndex(ColourRole::TEXT_SLIDER_THUMB)] = 0xFFE5F1FF;
    palette[colourRoleIndex(ColourRole::TEXT_SLIDER_METER_LOW)] = 0xFF54D68B;
    palette[colourRoleIndex(ColourRole::TEXT_SLIDER_METER_WARNING)] = 0xFFFFD166;
    palette[colourRoleIndex(ColourRole::TEXT_SLIDER_METER_HIGH)] = 0xFFFF6B6B;
    palette[colourRoleIndex(ColourRole::TOAST_BACKGROUND)] = 0xFF181818;
    palette[colourRoleIndex(ColourRole::QWERTY_WHITE_KEY_NOTE_TEXT)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::EQ_BAND_LOW)] = 0xFFFF8088;
    palette[colourRoleIndex(ColourRole::EQ_BAND_LOW_MID)] = 0xFF7FC5FF;
    palette[colourRoleIndex(ColourRole::EQ_BAND_HIGH_MID)] = 0xFFB0E890;
    palette[colourRoleIndex(ColourRole::EQ_BAND_HIGH)] = 0xFFFFD58C;
    palette[colourRoleIndex(ColourRole::ICON_BACKGROUND)] = 0xFF1B1B1B;
    palette[colourRoleIndex(ColourRole::ICON_BRIGHT)] = 0xFFF0F0F0;
    palette[colourRoleIndex(ColourRole::ICON_SUBTLE)] = 0xFFB8B8B8;
    palette[colourRoleIndex(ColourRole::ICON_MODULATION)] = 0xFFB8B8B8;
    palette[colourRoleIndex(ColourRole::ICON_BRAND)] = 0xFFF0F0F0;
    palette[colourRoleIndex(ColourRole::ICON_POWER)] = 0xFFE0E0E0;
    palette[colourRoleIndex(ColourRole::MIXER_FADER_THUMB)] = 0xFF2E4C66;
    palette[colourRoleIndex(ColourRole::MIXER_KNOB_OUTER)] = 0xFF1B1B1B;
    palette[colourRoleIndex(ColourRole::MIXER_KNOB_OUTER_STROKE)] = 0xFF707070;
    palette[colourRoleIndex(ColourRole::MIXER_KNOB_INNER)] = 0xFF101010;
    palette[colourRoleIndex(ColourRole::MIXER_KNOB_GUIDE)] = 0xFF808080;

    return palette;
}();

constexpr DarkTheme::SyntaxPalette highContrastSyntaxPalette = [] {
    auto palette = darkSyntaxPalette;

    palette[syntaxColourRoleIndex(SyntaxColourRole::EDITOR_BACKGROUND)] = 0xFF000000;
    palette[syntaxColourRoleIndex(SyntaxColourRole::EDITOR_DEFAULT_TEXT)] = 0xFFF5F5F5;
    palette[syntaxColourRoleIndex(SyntaxColourRole::LINE_NUMBER_BACKGROUND)] = 0xFF101010;
    palette[syntaxColourRoleIndex(SyntaxColourRole::LINE_NUMBER_TEXT)] = 0xFFB8B8B8;
    palette[syntaxColourRoleIndex(SyntaxColourRole::EDITOR_CARET)] = 0xFFFFFFFF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_CARET)] = 0xFFB8FFB8;
    palette[syntaxColourRoleIndex(SyntaxColourRole::EDITOR_SELECTION)] = 0x994DA3FF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_SELECTION)] = 0xFF174F80;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_STATUS_BACKGROUND)] = 0xFF147DCC;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_STATUS_TEXT)] = 0xFFFFFFFF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_OUTPUT_PROMPT)] = 0xFFB8FFB8;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_OUTPUT_INFO)] = 0xFF7FC5FF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_OUTPUT_TEXT)] = 0xFFE6E6E6;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_OUTPUT_ERROR)] = 0xFFFF9A8F;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_ERROR)] = 0xFFFF6B6B;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_COMMENT)] = 0xFFA6D59A;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_KEYWORD)] = 0xFF7FC5FF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_METHOD)] = 0xFFF1E3A0;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_PARAM)] = 0xFF9DD7FF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_OPERATOR)] = 0xFFE6E6E6;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_IDENTIFIER)] = 0xFFE6E6E6;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_NUMBER)] = 0xFFC6E8BC;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_STRING)] = 0xFFFFBE9C;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_BRACKET)] = 0xFFE6E6E6;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_PUNCTUATION)] = 0xFFE6E6E6;
    palette[syntaxColourRoleIndex(SyntaxColourRole::DSL_TOKEN_NOTE_NAME)] = 0xFF72DFC7;
    palette[syntaxColourRoleIndex(SyntaxColourRole::CHAT_TOKEN_TEXT)] = 0xFFE8E8E8;
    palette[syntaxColourRoleIndex(SyntaxColourRole::CHAT_TOKEN_PLUGIN_ALIAS)] = 0xFF7FC5FF;
    palette[syntaxColourRoleIndex(SyntaxColourRole::CHAT_TOKEN_PARAM_ALIAS)] = 0xFFFFC77D;
    palette[syntaxColourRoleIndex(SyntaxColourRole::CHAT_TOKEN_SLASH_COMMAND)] = 0xFF94E881;
    palette[syntaxColourRoleIndex(SyntaxColourRole::CHAT_TOKEN_PUNCTUATION)] = 0xFFE6E6E6;

    return palette;
}();

}  // namespace

DarkTheme::Palette DarkTheme::activePalette_ = darkPalette;
DarkTheme::SyntaxPalette DarkTheme::activeSyntaxPalette_ = darkSyntaxPalette;

const DarkTheme::Palette& DarkTheme::getDarkPalette() {
    return darkPalette;
}

const DarkTheme::Palette& DarkTheme::getActivePalette() {
    return activePalette_;
}

void DarkTheme::setActivePalette(const Palette& palette) {
    activePalette_ = palette;
}

void DarkTheme::resetToDarkPalette() {
    activePalette_ = darkPalette;
    activeSyntaxPalette_ = darkSyntaxPalette;
}

const DarkTheme::SyntaxPalette& DarkTheme::getDarkSyntaxPalette() {
    return darkSyntaxPalette;
}

const DarkTheme::SyntaxPalette& DarkTheme::getActiveSyntaxPalette() {
    return activeSyntaxPalette_;
}

void DarkTheme::setActiveSyntaxPalette(const SyntaxPalette& palette) {
    activeSyntaxPalette_ = palette;
}

bool ThemeManager::setActiveBuiltInTheme(const std::string& themeId) {
    if (themeId == kDarkThemeId) {
        DarkTheme::activePalette_ = darkPalette;
        DarkTheme::activeSyntaxPalette_ = darkSyntaxPalette;
        return true;
    }

    if (themeId == kLightThemeId) {
        DarkTheme::activePalette_ = lightPalette;
        DarkTheme::activeSyntaxPalette_ = lightSyntaxPalette;
        return true;
    }

    if (themeId == kHighContrastThemeId) {
        DarkTheme::activePalette_ = highContrastPalette;
        DarkTheme::activeSyntaxPalette_ = highContrastSyntaxPalette;
        return true;
    }

    return false;
}

bool ThemeManager::isBuiltInTheme(const std::string& themeId) {
    return themeId == kDarkThemeId || themeId == kLightThemeId || themeId == kHighContrastThemeId;
}

bool ThemeManager::isLightTheme() {
    return DarkTheme::getColour(DarkTheme::BACKGROUND).getPerceivedBrightness() >= 0.5f;
}

const DarkTheme::Palette& ThemeManager::builtInPalette(const std::string& themeId) {
    if (themeId == kLightThemeId)
        return lightPalette;
    if (themeId == kHighContrastThemeId)
        return highContrastPalette;
    return darkPalette;
}

const DarkTheme::SyntaxPalette& ThemeManager::builtInSyntaxPalette(const std::string& themeId) {
    if (themeId == kLightThemeId)
        return lightSyntaxPalette;
    if (themeId == kHighContrastThemeId)
        return highContrastSyntaxPalette;
    return darkSyntaxPalette;
}

std::optional<ColourRole> DarkTheme::findDarkPaletteRole(juce::Colour colour) {
    const auto rgb = colour.getARGB() & 0x00FFFFFFu;
    const auto findRole = [rgb](const Palette& palette) -> std::optional<ColourRole> {
        for (std::size_t index = 0; index < palette.size(); ++index)
            if ((palette[index] & 0x00FFFFFFu) == rgb)
                return static_cast<ColourRole>(index);

        return std::nullopt;
    };

    if (const auto activeRole = findRole(activePalette_))
        return activeRole;

    return findRole(darkPalette);
}

void DarkTheme::applyToSvgIcon(juce::Drawable& drawable) {
    // All replacements preserve the current Dark output exactly. On another
    // palette they let the control icon follow the corresponding semantic
    // colour without editing its SVG payload.
    drawable.replaceColour(juce::Colour(0xFFB3B3B3), getColour(ICON_NEUTRAL));
    drawable.replaceColour(juce::Colour(0xFFBCBCBC), getColour(ICON_TRANSPORT));
    drawable.replaceColour(juce::Colour(0xFF2E2E33), getColour(ICON_BACKGROUND));
    drawable.replaceColour(juce::Colour(0xFF1A1A1A), getColour(PIANO_ROLL_BACKGROUND));
    drawable.replaceColour(juce::Colour(0xFF1E1E1E), getColour(AUTOMATION_LANE_BACKGROUND));
    drawable.replaceColour(juce::Colour(0xFF444444), getColour(AUTOMATION_DIVIDER_LIGHT));
    drawable.replaceColour(juce::Colour(0xFF555555), getColour(BAR_LINE));
    drawable.replaceColour(juce::Colour(0xFF5588AA), getColour(ACCENT_BLUE));
    drawable.replaceColour(juce::Colour(0xFF7777DD), getColour(ACCENT_PURPLE));
    drawable.replaceColour(juce::Colour(0xFFAA4444), getColour(STATUS_ERROR));
    drawable.replaceColour(juce::Colour(0xFFFF8822), getColour(ACCENT_ORANGE));
    drawable.replaceColour(juce::Colour(0xFF66AAFF), getColour(ACCENT_CYAN));
    drawable.replaceColour(juce::Colour(0xFF88AACC), getColour(ACCENT_BLUE_LIGHT));
    drawable.replaceColour(juce::Colour(0xFFE3E3E3), getColour(ICON_BRIGHT));
    drawable.replaceColour(juce::Colour(0xFFE6E6E6), getColour(ICON_POWER));
    drawable.replaceColour(juce::Colour(0xFF808080), getColour(ICON_SUBTLE));
    drawable.replaceColour(juce::Colour(0xFF979797), getColour(ICON_MODULATION));
    drawable.replaceColour(juce::Colour(0xFFD9D9D9), getColour(ICON_BRAND));
    drawable.replaceColour(juce::Colour(0xFF2A3A4A), getColour(MIXER_FADER_THUMB));
    drawable.replaceColour(juce::Colour(0xFF2A2A35), getColour(MIXER_KNOB_OUTER));
    drawable.replaceColour(juce::Colour(0xFF3A3A45), getColour(MIXER_KNOB_OUTER_STROKE));
    drawable.replaceColour(juce::Colour(0xFF1E1E22), getColour(MIXER_KNOB_INNER));
    drawable.replaceColour(juce::Colour(0xFF404050), getColour(MIXER_KNOB_GUIDE));
    drawable.replaceColour(juce::Colours::black, getColour(ICON_NEUTRAL));
    drawable.replaceColour(juce::Colours::white, getColour(TEXT_BRIGHT));
}

juce::uint32 DarkTheme::getColourValue(ColourRole role) {
    const auto index = static_cast<std::size_t>(role);
    jassert(index < activePalette_.size());
    return activePalette_[index];
}

juce::uint32 DarkTheme::getSyntaxColourValue(SyntaxColourRole role) {
    const auto index = static_cast<std::size_t>(role);
    jassert(index < activeSyntaxPalette_.size());
    return activeSyntaxPalette_[index];
}

void DarkTheme::applyToLookAndFeel(juce::LookAndFeel_V4& laf) {
    // V4 ColourScheme drives the title bar background (widgetBackground) and a
    // few other top-level surfaces that the colour-ID system doesn't reach.
    laf.setColourScheme({
        getColour(BACKGROUND),        // windowBackground
        getColour(BACKGROUND),        // widgetBackground (DocumentWindow title bar)
        getColour(PANEL_BACKGROUND),  // menuBackground
        getColour(BORDER),            // outline
        getColour(TEXT_PRIMARY),      // defaultText
        getColour(BUTTON_NORMAL),     // defaultFill
        getColour(ICON_ON_ACCENT),    // highlightedText
        getColour(ACCENT_BLUE),       // highlightedFill
        getColour(TEXT_PRIMARY),      // menuText
    });

    // Background colors
    laf.setColour(juce::ResizableWindow::backgroundColourId, getColour(BACKGROUND));
    laf.setColour(juce::DocumentWindow::backgroundColourId, getColour(BACKGROUND));
    laf.setColour(juce::DocumentWindow::textColourId, getColour(TEXT_PRIMARY));

    // Text colors
    laf.setColour(juce::Label::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::TextEditor::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::TextEditor::backgroundColourId, getColour(SURFACE));
    laf.setColour(juce::TextEditor::outlineColourId, getColour(BORDER));
    laf.setColour(juce::TextEditor::focusedOutlineColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::CaretComponent::caretColourId, getColour(TEXT_PRIMARY));

    // Button colors
    laf.setColour(juce::TextButton::buttonColourId, getColour(BUTTON_NORMAL));
    laf.setColour(juce::TextButton::buttonOnColourId, getColour(BUTTON_ACTIVE));
    laf.setColour(juce::TextButton::textColourOffId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::TextButton::textColourOnId, getColour(ICON_ON_ACCENT));

    // Toggle button colors
    laf.setColour(juce::ToggleButton::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::ToggleButton::tickColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::ToggleButton::tickDisabledColourId, getColour(TEXT_DISABLED));

    // Slider colors
    laf.setColour(juce::Slider::backgroundColourId, getColour(SURFACE));
    laf.setColour(juce::Slider::thumbColourId, getColour(CONTROL_SLIDER_THUMB));
    laf.setColour(juce::Slider::trackColourId, getColour(BUTTON_NORMAL));
    laf.setColour(juce::Slider::rotarySliderFillColourId, getColour(CONTROL_VALUE_FILL));
    laf.setColour(juce::Slider::rotarySliderOutlineColourId, getColour(BORDER));
    laf.setColour(juce::Slider::textBoxTextColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::Slider::textBoxBackgroundColourId, getColour(SURFACE));
    laf.setColour(juce::Slider::textBoxOutlineColourId, getColour(BORDER));

    // ComboBox colors
    laf.setColour(juce::ComboBox::backgroundColourId, getColour(SURFACE));
    laf.setColour(juce::ComboBox::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::ComboBox::outlineColourId, getColour(BORDER));
    laf.setColour(juce::ComboBox::arrowColourId, getColour(TEXT_SECONDARY));
    laf.setColour(juce::ComboBox::buttonColourId, getColour(BUTTON_NORMAL));

    // PopupMenu colors
    laf.setColour(juce::PopupMenu::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::PopupMenu::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::PopupMenu::headerTextColourId, getColour(TEXT_SECONDARY));
    laf.setColour(juce::PopupMenu::highlightedBackgroundColourId, getColour(SURFACE_HOVER));
    laf.setColour(juce::PopupMenu::highlightedTextColourId, getColour(TEXT_PRIMARY));

    // Scrollbar colors
    laf.setColour(juce::ScrollBar::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::ScrollBar::thumbColourId, getColour(BUTTON_NORMAL));
    laf.setColour(juce::ScrollBar::trackColourId, getColour(SURFACE));

    // TreeView colors
    laf.setColour(juce::TreeView::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::TreeView::linesColourId, getColour(BORDER));
    laf.setColour(juce::TreeView::dragAndDropIndicatorColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::TreeView::selectedItemBackgroundColourId, getColour(SURFACE_HOVER));

    // ListBox colors
    laf.setColour(juce::ListBox::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::ListBox::outlineColourId, getColour(BORDER));
    laf.setColour(juce::ListBox::textColourId, getColour(TEXT_PRIMARY));

    // Toolbar colors
    laf.setColour(juce::Toolbar::backgroundColourId, getColour(TRANSPORT_BACKGROUND));
    laf.setColour(juce::Toolbar::separatorColourId, getColour(SEPARATOR));
    laf.setColour(juce::Toolbar::buttonMouseOverBackgroundColourId, getColour(BUTTON_HOVER));
    laf.setColour(juce::Toolbar::buttonMouseDownBackgroundColourId, getColour(BUTTON_PRESSED));

    // AlertWindow colors
    laf.setColour(juce::AlertWindow::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::AlertWindow::textColourId, getColour(TEXT_PRIMARY));
    laf.setColour(juce::AlertWindow::outlineColourId, getColour(BORDER));

    // TabbedComponent colors
    laf.setColour(juce::TabbedComponent::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::TabbedComponent::outlineColourId, getColour(BORDER));
    laf.setColour(juce::TabbedButtonBar::tabOutlineColourId, getColour(BORDER));
    laf.setColour(juce::TabbedButtonBar::tabTextColourId, getColour(TEXT_SECONDARY));
    laf.setColour(juce::TabbedButtonBar::frontOutlineColourId, getColour(ACCENT_BLUE));
    laf.setColour(juce::TabbedButtonBar::frontTextColourId, getColour(TEXT_PRIMARY));

    // PropertyPanel colors
    laf.setColour(juce::PropertyComponent::backgroundColourId, getColour(PANEL_BACKGROUND));
    laf.setColour(juce::PropertyComponent::labelTextColourId, getColour(TEXT_PRIMARY));

    // Note: CodeEditorComponent colors removed as they require additional JUCE modules
}

}  // namespace magda
