#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DarkTheme.hpp"
#include "FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief LookAndFeel for small toggle buttons with compact font and minimal rounding
 */
class SmallButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    SmallButtonLookAndFeel() = default;
    ~SmallButtonLookAndFeel() override = default;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        // Minimal corner radius (2% of smaller dimension)
        float cornerRadius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.02f;

        auto baseColour = bgColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRoundedRectangle(bounds, cornerRadius);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOverButton*/,
                        bool /*isButtonDown*/) override {
        auto font = FontManager::getInstance().getUIFontBold(9.0f);
        g.setFont(font);
        g.setColour(button
                        .findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        auto bounds = button.getLocalBounds().toFloat();
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred, false);
    }

    static SmallButtonLookAndFeel& getInstance() {
        static SmallButtonLookAndFeel instance;
        return instance;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmallButtonLookAndFeel)
};

}  // namespace magda::daw::ui
