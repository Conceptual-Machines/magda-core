#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DialogLookAndFeel.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"

namespace magda::daw::ui {

/**
 * @brief The Settings window's controls (#3162): switch toggles, small-caps section
 * headers over a rule, and the alert dialogs' buttons.
 */
class SettingsLookAndFeel : public DialogLookAndFeel {
  public:
    static inline const juce::Identifier kSectionHeader{"settingsSectionHeader"};
    static inline const juce::Identifier kLinkButton{"settingsLinkButton"};
    static inline const juce::Identifier kCheckBox{"settingsCheckBox"};

    /** @brief Draw @p label as a section header. */
    static void markSectionHeader(juce::Label& label) {
        label.getProperties().set(kSectionHeader, true);
        label.setFont(FontManager::getInstance().getUIFontMedium(11.0f));
        label.setColour(juce::Label::textColourId,
                        ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        label.setJustificationType(juce::Justification::bottomLeft);
    }

    /** @brief Draw @p toggle as a tick box rather than a switch, for a grid of them. */
    static void markCheckBox(juce::ToggleButton& toggle) {
        toggle.getProperties().set(kCheckBox, true);
    }

    /** @brief Draw @p button as plain text, like a link. */
    static void markLinkButton(juce::Button& button) {
        button.getProperties().set(kLinkButton, true);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over,
                          bool /*down*/) override {
        if (button.getProperties()[kCheckBox]) {
            drawCheckBox(g, button, over);
            return;
        }
        auto bounds = button.getLocalBounds().toFloat();
        const float alpha = button.isEnabled() ? 1.0f : 0.4f;

        const float height = juce::jmin(18.0f, bounds.getHeight() - 2.0f);
        const float width = juce::jmin(height * 1.8f, bounds.getWidth());
        const bool hasText = button.getButtonText().isNotEmpty();
        auto track = (hasText ? bounds.removeFromRight(width) : bounds.withWidth(width))
                         .withSizeKeepingCentre(width, height);

        const bool on = button.getToggleState();
        auto trackColour = on ? ActiveTheme::getAccentColour()
                              : ActiveTheme::getColour(ActiveTheme::SURFACE_HOVER);
        if (over)
            trackColour = trackColour.brighter(0.08f);
        g.setColour(trackColour.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(track, height * 0.5f);

        const float knob = height - 4.0f;
        const float knobX = on ? track.getRight() - knob - 2.0f : track.getX() + 2.0f;
        g.setColour(juce::Colours::white.withAlpha((on ? 0.95f : 0.7f) * alpha));
        g.fillEllipse(knobX, track.getY() + 2.0f, knob, knob);

        if (hasText) {
            auto textColour =
                button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(alpha);
            g.setFont(FontManager::getInstance().getUIFont(13.0f));
            drawLocalizedFittedText(g, button.getButtonText(),
                                    bounds.toNearestInt().withTrimmedRight(12),
                                    juce::Justification::centredLeft, 2, textColour);
        }
    }

    static void drawCheckBox(juce::Graphics& g, juce::ToggleButton& button, bool over) {
        const float alpha = button.isEnabled() ? 1.0f : 0.45f;
        const auto box = button.getLocalBounds().toFloat().withSizeKeepingCentre(15.0f, 15.0f);
        if (button.getToggleState()) {
            g.setColour(ActiveTheme::getAccentColour().withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(box, 3.0f);
            juce::Path tick;
            tick.startNewSubPath(box.getX() + 3.5f, box.getCentreY());
            tick.lineTo(box.getX() + 6.5f, box.getBottom() - 4.0f);
            tick.lineTo(box.getRight() - 3.5f, box.getY() + 4.0f);
            g.setColour(juce::Colours::white.withAlpha(alpha));
            g.strokePath(tick, juce::PathStrokeType(1.6f));
        } else {
            g.setColour(ActiveTheme::getBorderColour()
                            .brighter(over ? 0.3f : 0.0f)
                            .withMultipliedAlpha(alpha));
            g.drawRoundedRectangle(box.reduced(0.5f), 3.0f, 1.0f);
        }
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override {
        if (!label.getProperties()[kSectionHeader]) {
            DialogLookAndFeel::drawLabel(g, label);
            return;
        }
        auto bounds = label.getLocalBounds();
        g.setColour(ActiveTheme::getBorderColour());
        g.fillRect(bounds.removeFromBottom(1));
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont().withExtraKerningFactor(0.08f));
        g.drawText(label.getText().toUpperCase(), bounds.withTrimmedBottom(5),
                   juce::Justification::bottomLeft, true);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool over, bool down) override {
        if (!button.getProperties()[kLinkButton])
            drawAlertButtonBackground(g, button, over, down);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool over,
                        bool /*down*/) override {
        if (!button.getProperties()[kLinkButton]) {
            drawAlertButtonText(g, button);
            return;
        }
        auto colour =
            ActiveTheme::getColour(over ? ActiveTheme::TEXT_PRIMARY : ActiveTheme::TEXT_SECONDARY);
        g.setColour(colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawText(button.getButtonText(), button.getLocalBounds(),
                   juce::Justification::centredLeft, true);
    }

    static SettingsLookAndFeel& getInstance() {
        static SettingsLookAndFeel instance;
        return instance;
    }
};

}  // namespace magda::daw::ui
