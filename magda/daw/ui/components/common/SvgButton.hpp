#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "../../themes/DarkTheme.hpp"
#include "../../utils/ComponentManager.hpp"

namespace magda {

class SvgButton : public juce::Button {
  public:
    // Single icon constructor (legacy - colors icon based on state)
    SvgButton(const juce::String& buttonName, const char* svgData, size_t svgDataSize);

    // Dual icon constructor (uses separate off/on images with pre-baked colors)
    SvgButton(const juce::String& buttonName, const char* offSvgData, size_t offSvgDataSize,
              const char* onSvgData, size_t onSvgDataSize);

    ~SvgButton() override;

    // Button overrides
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // Update SVG data
    void updateSvgData(const char* svgData, size_t svgDataSize);

    // Set custom colors (only used in single-icon mode)
    void setNormalColor(juce::Colour color) {
        normalColor = color;
        normalColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasNormalColor_ = true;
    }
    void setHoverColor(juce::Colour color) {
        hoverColor = color;
        hoverColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasHoverColor_ = true;
    }
    void setPressedColor(juce::Colour color) {
        pressedColor = color;
        pressedColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasPressedColor_ = true;
    }
    void setActiveColor(juce::Colour color) {
        activeColor = color;
        activeColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasActiveColor_ = true;
    }
    void setActiveBackgroundColor(juce::Colour color) {
        activeBackgroundColor = color;
        activeBackgroundColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasActiveBackgroundColor = true;
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

    // Border settings
    void setIconPadding(float padding) {
        iconPadding = padding;
    }

    void setInactiveIconOpacity(float opacity) {
        inactiveIconOpacity = juce::jlimit(0.0f, 1.0f, opacity);
    }

    void setBorderColor(juce::Colour color) {
        borderColor = color;
        borderColorRole_ = DarkTheme::findDarkPaletteRole(color);
        hasBorder = true;
    }
    // Border colour used while active/engaged (falls back to borderColor).
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
