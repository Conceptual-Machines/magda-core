#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/DarkTheme.hpp"

namespace magica {

class SvgButton : public juce::Button {
  public:
    SvgButton(const juce::String& buttonName, const char* svgData, size_t svgDataSize);
    ~SvgButton() override = default;

    // Button overrides
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    // Update SVG data
    void updateSvgData(const char* svgData, size_t svgDataSize);

    // Set custom colors
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

    // Set button as toggle/active state
    void setActive(bool isActive) {
        active = isActive;
        repaint();
    }
    bool isActive() const {
        return active;
    }

  private:
    std::unique_ptr<juce::Drawable> svgIcon;

    // Colors for different states
    juce::Colour normalColor = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY);
    juce::Colour hoverColor = DarkTheme::getColour(DarkTheme::TEXT_PRIMARY);
    juce::Colour pressedColor = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
    juce::Colour activeColor = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);

    bool active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvgButton)
};

}  // namespace magica
