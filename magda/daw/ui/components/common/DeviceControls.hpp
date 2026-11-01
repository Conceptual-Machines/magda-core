// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/common/TextSlider.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

/**
 * @file DeviceControls.hpp
 * @brief Fields, toggles and choices in the faceplates' device style, for the panels that sit
 *        beside a faceplate rather than inside one.
 */

namespace magda::daw::ui {

namespace device_controls {

constexpr float kRadius = 4.0f;

/** @brief The rounded well a value or a choice sits in. */
inline void paintField(juce::Graphics& g, juce::Rectangle<float> area, float alpha = 1.0f) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(area, kRadius);
    g.setColour(
        ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(area.reduced(0.5f), kRadius, 1.0f);
}

/** @brief The background a plot or a live display draws on. */
inline void paintGraph(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(area, kRadius);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(area.reduced(0.5f), kRadius, 1.0f);
}

/** @brief Captions, tabs and toggle labels. */
inline juce::Font labelFont(float size = 10.0f) {
    return FontManager::getInstance().getMonoFont(size).withExtraKerningFactor(0.08f);
}

/** @brief A value slider over a painted field: no frame or fill of its own. */
inline void styleValue(TextSlider& slider) {
    slider.setDrawFrame(false);
    slider.setShowFillIndicator(false);
    slider.setBackgroundColour(juce::Colours::transparentBlack);
    slider.setFont(FontManager::getInstance().getUIFontMedium(10.5f));
    slider.setTextColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT));
}

}  // namespace device_controls

/**
 * @brief Combo boxes and text buttons drawn as device fields.
 *
 * A toggle lights while it is on; a plain button reads as a field with its label. Popup menus
 * keep the small combo style.
 */
class DeviceControlLookAndFeel : public SmallComboBoxLookAndFeel {
  public:
    void drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        const float alpha = box.isEnabled() ? 1.0f : 0.4f;
        device_controls::paintField(g, juce::Rectangle<int>(width, height).toFloat(), alpha);

        constexpr float arrowW = 6.0f;
        constexpr float arrowH = 3.5f;
        const float x = static_cast<float>(width) - arrowW - 6.0f;
        const float y = static_cast<float>(height) * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(x, y - arrowH * 0.5f);
        arrow.lineTo(x + arrowW * 0.5f, y + arrowH * 0.5f);
        arrow.lineTo(x + arrowW, y - arrowH * 0.5f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM).withMultipliedAlpha(alpha));
        g.strokePath(arrow, juce::PathStrokeType(1.2f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(2, 0, box.getWidth() - 16, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }

    juce::Font getComboBoxFont(juce::ComboBox& /*box*/) override {
        return FontManager::getInstance().getUIFontMedium(10.5f);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override {
        if (label.isBeingEdited())
            return;
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_VALUE_TEXT)
                        .withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.5f));
        g.setFont(getLabelFont(label));
        g.drawFittedText(label.getText(),
                         label.getBorderSize().subtractedFrom(label.getLocalBounds()),
                         label.getJustificationType(), 1, label.getMinimumHorizontalScale());
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& /*backgroundColour*/, bool isMouseOver,
                              bool isButtonDown) override {
        const auto bounds = button.getLocalBounds().toFloat();
        const float alpha = button.isEnabled() ? 1.0f : 0.4f;
        if (button.getToggleState()) {
            g.setColour(
                ActiveTheme::getColour(ActiveTheme::DEVICE_LINE2).withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(bounds, device_controls::kRadius);
        } else {
            device_controls::paintField(g, bounds, alpha);
        }
        if (button.isEnabled() && (isMouseOver || isButtonDown)) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
            g.drawRoundedRectangle(bounds.reduced(0.5f), device_controls::kRadius, 1.0f);
        }
    }

    juce::Font getTextButtonFont(juce::TextButton& /*button*/, int /*buttonHeight*/) override {
        return device_controls::labelFont();
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool /*isMouseOver*/,
                        bool /*isButtonDown*/) override {
        const bool lit = button.getToggleState() || !button.getClickingTogglesState();
        g.setColour(ActiveTheme::getColour(lit ? ActiveTheme::DEVICE_TEXT : ActiveTheme::DEVICE_DIM)
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
        g.setFont(device_controls::labelFont());
        g.drawFittedText(button.getButtonText().toUpperCase(),
                         button.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1,
                         1.0f);
    }

    static DeviceControlLookAndFeel& getInstance() {
        static DeviceControlLookAndFeel instance;
        return instance;
    }
};

}  // namespace magda::daw::ui
