#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
    }
    void setHoverColor(juce::Colour color) {
        hoverColor = color;
    }
    void setPressedColor(juce::Colour color) {
        pressedColor = color;
    }
    void setActiveColor(juce::Colour color) {
        activeColor = color;
    }
    void setActiveBackgroundColor(juce::Colour color) {
        activeBackgroundColor = color;
        hasActiveBackgroundColor = true;
    }
    void setNormalBackgroundColor(juce::Colour color) {
        normalBackgroundColor = color;
        hasNormalBackgroundColor = true;
    }
    void setOriginalColor(juce::Colour color) {
        originalColor = color;
        hasOriginalColor = true;
    }

    // Border settings
    void setBorderColor(juce::Colour color) {
        borderColor = color;
        hasBorder = true;
    }
    void setBorderThickness(float thickness) {
        borderThickness = thickness;
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
    magda::ManagedDrawable svgIcon;
    magda::ManagedDrawable svgIconOff;
    magda::ManagedDrawable svgIconOn;

    bool dualIconMode = false;

    // Colors for different states (single-icon mode only)
    juce::Colour normalColor = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    juce::Colour hoverColor = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    juce::Colour pressedColor = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    juce::Colour activeColor = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    juce::Colour originalColor;  // Original SVG fill color to replace
    bool hasOriginalColor = false;
    juce::Colour activeBackgroundColor;  // Background color when active
    bool hasActiveBackgroundColor = false;
    juce::Colour normalBackgroundColor;  // Background color in normal state
    bool hasNormalBackgroundColor = false;

    // Border settings
    juce::Colour borderColor;
    float borderThickness = 1.0f;
    bool hasBorder = false;

    bool active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvgButton)
};

}  // namespace magda
