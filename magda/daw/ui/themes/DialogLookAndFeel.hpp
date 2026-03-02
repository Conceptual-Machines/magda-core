#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DarkTheme.hpp"
#include "FontManager.hpp"

namespace magda::daw::ui {

/**
 * @brief LookAndFeel for dialog windows — dark colour scheme, Inter font everywhere
 */
class DialogLookAndFeel : public juce::LookAndFeel_V4 {
  public:
    DialogLookAndFeel() : juce::LookAndFeel_V4(getDarkColourScheme()) {
        DarkTheme::applyToLookAndFeel(*this);
    }
    ~DialogLookAndFeel() override = default;

    // ── TextButton ──────────────────────────────────────────────────────

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& bgColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
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

    juce::Font getTextButtonFont(juce::TextButton&, int /*buttonHeight*/) override {
        return FontManager::getInstance().getButtonFont(13.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOverButton*/,
                        bool /*isButtonDown*/) override {
        auto font = FontManager::getInstance().getButtonFont(13.0f);
        g.setFont(font);
        g.setColour(button
                        .findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));

        auto bounds = button.getLocalBounds().toFloat();
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred, false);
    }

    // ── ToggleButton ────────────────────────────────────────────────────

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override {
        float fontSize = juce::jmin(15.0f, static_cast<float>(button.getHeight()) * 0.75f);
        float tickWidth = fontSize * 1.1f;

        drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tickWidth) * 0.5f,
                    tickWidth, tickWidth, button.getToggleState(), button.isEnabled(),
                    shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(FontManager::getInstance().getUIFont(fontSize));

        if (!button.isEnabled())
            g.setOpacity(0.5f);

        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds()
                             .withTrimmedLeft(juce::roundToInt(tickWidth) + 10)
                             .withTrimmedRight(2),
                         juce::Justification::centredLeft, 10);
    }

    // ── ComboBox ────────────────────────────────────────────────────────

    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
        float cornerRadius = 2.0f;

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, cornerRadius);

        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);

        // Chevron arrow
        const float arrowHeight = 4.0f;
        const float arrowWidth = 6.0f;
        const float arrowX = width - arrowWidth - 8.0f;
        const float arrowY = height / 2.0f;

        juce::Path arrow;
        arrow.startNewSubPath(arrowX, arrowY - arrowHeight / 2.0f);
        arrow.lineTo(arrowX + arrowWidth / 2.0f, arrowY + arrowHeight / 2.0f);
        arrow.lineTo(arrowX + arrowWidth, arrowY - arrowHeight / 2.0f);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.strokePath(arrow, juce::PathStrokeType(1.2f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        const int arrowSpace = 18;
        label.setBounds(6, 0, box.getWidth() - arrowSpace, box.getHeight());
        label.setFont(FontManager::getInstance().getUIFont(13.0f));
    }

    juce::Font getComboBoxFont(juce::ComboBox& /*box*/) override {
        return FontManager::getInstance().getUIFont(13.0f);
    }

    // ── PopupMenu ───────────────────────────────────────────────────────

    juce::Font getPopupMenuFont() override {
        return FontManager::getInstance().getUIFont(13.0f);
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

    // ── Label ───────────────────────────────────────────────────────────

    juce::Font getLabelFont(juce::Label& label) override {
        // Respect fonts set directly via setFont(); fall back to Inter
        if (label.getFont() != juce::Font(juce::FontOptions()))
            return label.getFont();
        return FontManager::getInstance().getUIFont(14.0f);
    }

    // ── Slider ──────────────────────────────────────────────────────────

    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(FontManager::getInstance().getUIFont(12.0f));
        return label;
    }

    // ── TabbedComponent ─────────────────────────────────────────────────

    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver,
                       bool isMouseDown) override {
        auto area = button.getActiveArea();
        auto o = button.getTabbedButtonBar().getOrientation();
        auto isFrontTab = button.isFrontTab();

        // Background
        if (isFrontTab) {
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
        } else if (isMouseOver) {
            g.setColour(DarkTheme::getColour(DarkTheme::SURFACE_HOVER));
        } else {
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
        }
        g.fillRect(area);

        // Bottom accent for front tab, subtle separator for others
        if (o == juce::TabbedButtonBar::TabsAtTop || o == juce::TabbedButtonBar::TabsAtBottom) {
            if (isFrontTab) {
                g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
                if (o == juce::TabbedButtonBar::TabsAtTop)
                    g.fillRect(area.getX(), area.getBottom() - 2, area.getWidth(), 2);
                else
                    g.fillRect(area.getX(), area.getY(), area.getWidth(), 2);
            } else {
                // Bottom border for non-selected tabs
                g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
                if (o == juce::TabbedButtonBar::TabsAtTop)
                    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);
                else
                    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1);
            }
        }

        // Text
        auto textColour =
            isFrontTab ? DarkTheme::getTextColour() : DarkTheme::getSecondaryTextColour();
        if (isMouseDown)
            textColour = textColour.brighter(0.2f);

        g.setColour(textColour);
        g.setFont(FontManager::getInstance().getUIFontMedium(13.0f));
        g.drawFittedText(button.getButtonText(), button.getTextArea(), juce::Justification::centred,
                         1);
    }

    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override {
        // Intentionally empty — tab backgrounds are fully handled by drawTabButton()
    }

    // ── Singleton ───────────────────────────────────────────────────────

    static DialogLookAndFeel& getInstance() {
        static DialogLookAndFeel instance;
        return instance;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogLookAndFeel)
};

}  // namespace magda::daw::ui
