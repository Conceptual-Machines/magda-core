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
        // Slightly rounded corners
        float cornerRadius = 3.0f;

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

/**
 * @brief LookAndFeel for flat tab-style toggle buttons with no rounding
 */
class FlatTabButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    FlatTabButtonLookAndFeel() = default;
    ~FlatTabButtonLookAndFeel() override = default;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat();

        auto baseColour = bgColour;
        if (shouldDrawButtonAsDown)
            baseColour = baseColour.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            baseColour = baseColour.brighter(0.1f);

        g.setColour(baseColour);
        g.fillRect(bounds);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(bounds, 0.5f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOverButton*/,
                        bool /*isButtonDown*/) override {
        auto font = FontManager::getInstance().getUIFontBold(9.0f);
        g.setFont(font);
        g.setColour(button
                        .findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        g.drawText(button.getButtonText(), button.getLocalBounds().toFloat(),
                   juce::Justification::centred, false);
    }

    static FlatTabButtonLookAndFeel& getInstance() {
        static FlatTabButtonLookAndFeel instance;
        return instance;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlatTabButtonLookAndFeel)
};

}  // namespace magda::daw::ui
