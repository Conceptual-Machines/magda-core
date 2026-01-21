#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief LookAndFeel for small toggle buttons with compact font
 */
class SmallButtonLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    SmallButtonLookAndFeel() = default;
    ~SmallButtonLookAndFeel() override = default;

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
