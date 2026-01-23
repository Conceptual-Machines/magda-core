#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DarkTheme.hpp"
#include "FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief LookAndFeel for small combo boxes with compact font and chevron arrow
 */
class SmallComboBoxLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    SmallComboBoxLookAndFeel() = default;
    ~SmallComboBoxLookAndFeel() override = default;

    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        float cornerRadius = 2.0f;

        // Background
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Border
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);

        // Draw chevron arrow on the right
        const float arrowHeight = 4.0f;
        const float arrowWidth = 6.0f;
        const float arrowX = width - arrowWidth - 5.0f;
        const float arrowY = height / 2.0f;

        juce::Path arrow;
        arrow.startNewSubPath(arrowX, arrowY - arrowHeight / 2.0f);
        arrow.lineTo(arrowX + arrowWidth / 2.0f, arrowY + arrowHeight / 2.0f);
        arrow.lineTo(arrowX + arrowWidth, arrowY - arrowHeight / 2.0f);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.strokePath(arrow, juce::PathStrokeType(1.2f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        // Leave space for arrow on the right
        const int arrowSpace = 14;
        label.setBounds(4, 0, box.getWidth() - arrowSpace, box.getHeight());
        label.setFont(FontManager::getInstance().getUIFont(9.0f));
    }

    juce::Font getComboBoxFont(juce::ComboBox& /*box*/) override {
        return FontManager::getInstance().getUIFont(9.0f);
    }

    juce::Font getPopupMenuFont() override {
        return FontManager::getInstance().getUIFont(9.0f);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override {
        if (isSeparator) {
            auto separatorArea = area.reduced(5, 0).withHeight(1);
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
            g.fillRect(separatorArea);
            return;
        }

        auto textArea = area.reduced(8, 0);

        if (isHighlighted && isActive) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.3f));
            g.fillRect(area);
        }

        g.setColour(textColour != nullptr ? *textColour
                                          : (isActive ? DarkTheme::getTextColour()
                                                      : DarkTheme::getSecondaryTextColour()));
        g.setFont(getPopupMenuFont());
        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

        juce::ignoreUnused(isTicked, hasSubMenu, shortcutKeyText, icon);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        g.fillRect(0, 0, width, height);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRect(0, 0, width, height);
    }

    static SmallComboBoxLookAndFeel& getInstance() {
        static SmallComboBoxLookAndFeel instance;
        return instance;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmallComboBoxLookAndFeel)
};

}  // namespace magda::daw::ui
