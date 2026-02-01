#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * SideFX-inspired dark theme for MAGDA
 * Color palette derived from professional audio production interfaces
 */
class DarkTheme {
  public:
    // ==========================================================================
    // Background colors (dark with subtle purple/blue tints)
    // ==========================================================================
    static constexpr auto BACKGROUND = 0xFF1A1A1A;        // Main background
    static constexpr auto BACKGROUND_ALT = 0xFF1E1E22;    // Charcoal (slight blue tint)
    static constexpr auto PANEL_BACKGROUND = 0xFF252530;  // Panel background (purple tint)
    static constexpr auto SURFACE = 0xFF2A2A35;           // Elevated surface
    static constexpr auto SURFACE_HOVER = 0xFF333333;     // Hovered surface

    // ==========================================================================
    // Transport and controls
    // ==========================================================================
    static constexpr auto TRANSPORT_BACKGROUND = 0xFF1E1E22;  // Transport bar background
    static constexpr auto BUTTON_NORMAL = 0xFF1A1A1A;         // Normal button (off state)
    static constexpr auto BUTTON_HOVER = 0xFF2A2A35;          // Hovered button
    static constexpr auto BUTTON_PRESSED = 0xFF333333;        // Pressed button
    static constexpr auto BUTTON_ACTIVE = 0xFF5588AA;  // Active/selected button (SideFX blue)
    static constexpr auto BUTTON_STROKE = 0xFF444444;  // Button border

    // ==========================================================================
    // Text colors
    // ==========================================================================
    static constexpr auto TEXT_PRIMARY = 0xFFDDDDDD;    // Primary text (soft white)
    static constexpr auto TEXT_SECONDARY = 0xFFAABBCC;  // Secondary text (blue-tinted)
    static constexpr auto TEXT_DIM = 0xFF888888;        // Dimmed text
    static constexpr auto TEXT_DISABLED = 0xFF666666;   // Disabled text

    // ==========================================================================
    // Accent colors (SideFX palette)
    // ==========================================================================
    static constexpr auto ACCENT_BLUE = 0xFF5588AA;        // Primary accent (muted blue)
    static constexpr auto ACCENT_BLUE_LIGHT = 0xFF88AACC;  // Light blue
    static constexpr auto ACCENT_CYAN = 0xFF66AAFF;        // Cyan (selection, highlight)
    static constexpr auto ACCENT_GREEN = 0xFF33E680;       // Bright green (curve, enabled)
    static constexpr auto ACCENT_ORANGE = 0xFFFF8822;      // Orange (nodes, playhead)
    static constexpr auto ACCENT_PURPLE = 0xFF7777DD;      // Purple accent

    // ==========================================================================
    // Status colors
    // ==========================================================================
    static constexpr auto STATUS_SUCCESS = 0xFF44AA44;  // Success/enabled (green)
    static constexpr auto STATUS_WARNING = 0xFFFFAA44;  // Warning (orange)
    static constexpr auto STATUS_ERROR = 0xFFAA4444;    // Error (muted red)
    static constexpr auto STATUS_DANGER = 0xFFFF6644;   // Danger/record (bright red-orange)

    // Backwards compatibility aliases
    static constexpr auto ACCENT_RED = STATUS_DANGER;  // Alias for STATUS_DANGER

    // ==========================================================================
    // Track colors
    // ==========================================================================
    static constexpr auto TRACK_BACKGROUND = 0xFF1E1E22;  // Track background
    static constexpr auto TRACK_SELECTED = 0xFF2A2A35;    // Selected track
    static constexpr auto TRACK_SEPARATOR = 0xFF1A1A1A;   // Track separator lines

    // ==========================================================================
    // Timeline and grid
    // ==========================================================================
    static constexpr auto TIMELINE_BACKGROUND = 0xFF1E1E22;  // Timeline background
    static constexpr auto GRID_LINE = 0xFF383840;            // Grid lines
    static constexpr auto BEAT_LINE = 0xFF484850;            // Beat lines (stronger)
    static constexpr auto BAR_LINE = 0xFF555555;             // Bar lines (strongest)

    // ==========================================================================
    // Borders and separators
    // ==========================================================================
    static constexpr auto BORDER = 0xFF444444;         // General borders
    static constexpr auto SEPARATOR = 0xFF333333;      // Panel separators
    static constexpr auto RESIZE_HANDLE = 0xFF555555;  // Resize handles

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
    static constexpr auto LOOP_REGION = 0x3344AA66;     // Semi-transparent green for loop region
    static constexpr auto LOOP_MARKER = 0xFF44AA66;     // Solid green for loop flag markers
    static constexpr auto OFFSET_MARKER = 0xFFCCAA44;   // Solid yellow for content offset marker

    // Apply the theme to JUCE's LookAndFeel
    static void applyToLookAndFeel(juce::LookAndFeel& laf);

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
