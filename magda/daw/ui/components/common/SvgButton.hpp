#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

#include "../../themes/DarkTheme.hpp"
#include "../../utils/ComponentManager.hpp"

namespace magda {

class SvgButton : public juce::Button {
  public:
    // Single icon constructor (legacy - colors icon based on state)
    SvgButton(const juce::String& buttonName, const char* svgData, size_t svgDataSize);

    // Dual icon constructor retained for controls whose states genuinely use
    // different geometry. Colour-only state changes should use one SVG plus
    // setStateColourReplacement().
    SvgButton(const juce::String& buttonName, const char* offSvgData, size_t offSvgDataSize,
              const char* onSvgData, size_t onSvgDataSize);

    ~SvgButton() override;

    // Button overrides
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // Update SVG data
    void updateSvgData(const char* svgData, size_t svgDataSize);

    // Set custom colors (only used in single-icon mode). Two kinds of caller:
    // theme-sourced colours should use the ColourRole overloads (exact, always
    // follow live theme changes; a raw palette colour is back-resolved by RGB
    // match, which can alias when several roles share a value). Derived or
    // dynamic colours (track/modulator colours, alpha-blended chips) use the
    // juce::Colour overloads and intentionally stay literal.
    void setNormalColor(ColourRole role) {
        normalColor = DarkTheme::getColour(role);
        normalColorRole_ = role;
        hasNormalColor_ = true;
    }
    void setNormalColor(juce::Colour color) {
        normalColor = color;
        normalColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasNormalColor_ = true;
    }
    void setHoverColor(ColourRole role) {
        hoverColor = DarkTheme::getColour(role);
        hoverColorRole_ = role;
        hasHoverColor_ = true;
    }
    void setHoverColor(juce::Colour color) {
        hoverColor = color;
        hoverColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasHoverColor_ = true;
    }
    void setPressedColor(ColourRole role) {
        pressedColor = DarkTheme::getColour(role);
        pressedColorRole_ = role;
        hasPressedColor_ = true;
    }
    void setPressedColor(juce::Colour color) {
        pressedColor = color;
        pressedColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasPressedColor_ = true;
    }
    void setActiveColor(ColourRole role) {
        activeColor = DarkTheme::getColour(role);
        activeColorRole_ = role;
        hasActiveColor_ = true;
    }
    void setActiveColor(juce::Colour color) {
        activeColor = color;
        activeColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasActiveColor_ = true;
    }
    void setActiveBackgroundColor(ColourRole role) {
        activeBackgroundColor = DarkTheme::getColour(role);
        activeBackgroundColorRole_ = role;
        hasActiveBackgroundColor = true;
    }
    void setActiveBackgroundColor(juce::Colour color) {
        activeBackgroundColor = color;
        activeBackgroundColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasActiveBackgroundColor = true;
    }
    void setNormalBackgroundColor(ColourRole role) {
        normalBackgroundColor = DarkTheme::getColour(role);
        normalBackgroundColorRole_ = role;
        hasNormalBackgroundColor = true;
    }
    void setNormalBackgroundColor(juce::Colour color) {
        normalBackgroundColor = color;
        normalBackgroundColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasNormalBackgroundColor = true;
    }
    void setOriginalColor(juce::Colour color) {
        originalColor = color;
        hasOriginalColor = true;
    }

    // Recolour one stable SVG source key according to the button state. This
    // keeps geometry in the asset and all visual state in code.
    void setStateColourReplacement(juce::Colour sourceColour, juce::Colour inactiveColour,
                                   juce::Colour activeColour);
    void setStateColourReplacement(juce::Colour sourceColour, ColourRole inactiveRole,
                                   ColourRole activeRole);

    // Border settings
    void setIconPadding(float padding) {
        iconPadding = padding;
    }

    void setInactiveIconOpacity(float opacity) {
        inactiveIconOpacity = juce::jlimit(0.0f, 1.0f, opacity);
    }

    void setBorderColor(ColourRole role) {
        borderColor = DarkTheme::getColour(role);
        borderColorRole_ = role;
        hasBorder = true;
    }
    void setBorderColor(juce::Colour color) {
        borderColor = color;
        borderColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasBorder = true;
    }
    // Border colour used while active/engaged (falls back to borderColor).
    void setActiveBorderColor(ColourRole role) {
        activeBorderColor = DarkTheme::getColour(role);
        activeBorderColorRole_ = role;
        hasActiveBorderColor = true;
        hasBorder = true;
    }
    void setActiveBorderColor(juce::Colour color) {
        activeBorderColor = color;
        activeBorderColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasActiveBorderColor = true;
        hasBorder = true;
    }
    void setBorderThickness(float thickness) {
        borderThickness = thickness;
    }
    void setCornerRadius(float radius) {
        cornerRadius = radius;
    }

    // Set button as toggle/active state
    void setActive(bool isActive) {
        active = isActive;
        repaint();
    }
    bool isActive() const {
        return active;
    }

  private:
    static juce::Colour resolveThemeColour(juce::Colour colour,
                                           const std::optional<ColourRole>& role);

    // Applies the per-button tints and then the generic themed SVG mapping.
    // Tints are staged through sentinel keys so the generic mapping cannot
    // re-map a tint whose resolved colour collides with one of its source
    // colours (e.g. a white glyph tint on an accent chip would otherwise be
    // swallowed by the white -> TEXT_BRIGHT mapping and drift with the theme).
    // glyphTint, when set, recolours the glyph key (originalColor, or
    // black/currentColor for untinted legacy assets); dual-icon mode passes
    // nullopt since its state lives in the pre-baked on/off images.
    void applyThemedTints(juce::Drawable& icon, bool drawOn,
                          const std::optional<juce::Colour>& glyphTint) const;

    struct StateColourReplacement {
        juce::Colour source;
        juce::Colour inactive;
        juce::Colour active;
        std::optional<ColourRole> inactiveRole;
        std::optional<ColourRole> activeRole;
    };

    magda::ManagedDrawable svgIcon;
    magda::ManagedDrawable svgIconOff;
    magda::ManagedDrawable svgIconOn;

    bool dualIconMode = false;

    // Colors for different states (single-icon mode only)
    juce::Colour normalColor;
    juce::Colour hoverColor;
    juce::Colour pressedColor;
    juce::Colour activeColor;
    std::optional<ColourRole> normalColorRole_;
    std::optional<ColourRole> hoverColorRole_;
    std::optional<ColourRole> pressedColorRole_;
    std::optional<ColourRole> activeColorRole_;
    bool hasNormalColor_ = false;
    bool hasHoverColor_ = false;
    bool hasPressedColor_ = false;
    bool hasActiveColor_ = false;
    juce::Colour originalColor;  // Original SVG fill color to replace
    bool hasOriginalColor = false;
    std::vector<StateColourReplacement> stateColourReplacements_;
    juce::Colour activeBackgroundColor;  // Background color when active
    std::optional<ColourRole> activeBackgroundColorRole_;
    bool hasActiveBackgroundColor = false;
    juce::Colour normalBackgroundColor;  // Background color in normal state
    std::optional<ColourRole> normalBackgroundColorRole_;
    bool hasNormalBackgroundColor = false;

    // Border settings
    juce::Colour borderColor;
    std::optional<ColourRole> borderColorRole_;
    juce::Colour activeBorderColor;  // Border colour when active (if set)
    std::optional<ColourRole> activeBorderColorRole_;
    bool hasActiveBorderColor = false;
    float borderThickness = 1.0f;
    float cornerRadius = 2.0f;
    bool hasBorder = false;
    float iconPadding = 4.0f;
    float inactiveIconOpacity = 1.0f;

    bool active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvgButton)
};

}  // namespace magda
