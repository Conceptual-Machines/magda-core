// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../../../themes/ActiveTheme.hpp"
#include "../../../../themes/FontManager.hpp"
#include "../../../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../../../themes/SmallButtonLookAndFeel.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"

namespace magda::daw::ui::clip_style {

constexpr int kFieldHeight = 24;
constexpr int kCaptionHeight = 16;
constexpr int kGap = 6;

/** @brief A field's name above or beside it: "Gain", "Tempo". */
inline void caption(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(11.0f));
    label.setColour(juce::Label::textColourId, ActiveTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    label.setBorderSize({});
    label.setInterceptsMouseClicks(false, false);
}

/** @brief A column heading in small spaced capitals: START, END, PHASE. */
inline void columnHeading(juce::Label& label, const juce::String& text) {
    label.setText(text.toUpperCase(), juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getMonoFont(9.0f).withExtraKerningFactor(0.1f));
    label.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    label.setJustificationType(juce::Justification::centredRight);
    label.setBorderSize({});
    label.setInterceptsMouseClicks(false, false);
}

/** @brief A numeric field: mono digits right-aligned, the unit dimmed after them. */
inline void field(magda::DraggableValueLabel& value, const juce::String& unit = {}) {
    value.setFont(FontManager::getInstance().getMonoFont(11.0f));
    value.setJustification(juce::Justification::centredRight);
    value.setShowFillIndicator(false);
    value.setUnit(unit);
}

/** @brief A text toggle: accent fill and accent text while on. */
inline void toggle(juce::TextButton& button) {
    const auto accent = ActiveTheme::getAccentColour();
    button.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    button.setColour(juce::TextButton::buttonColourId,
                     ActiveTheme::getColour(ActiveTheme::SURFACE));
    button.setColour(juce::TextButton::buttonOnColourId, accent.withAlpha(0.22f));
    button.setColour(juce::TextButton::textColourOffId, ActiveTheme::getSecondaryTextColour());
    button.setColour(juce::TextButton::textColourOnId, accent);
}

/** @brief A momentary button: ÷2, +12. */
inline void action(juce::TextButton& button) {
    button.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    button.setColour(juce::TextButton::buttonColourId,
                     ActiveTheme::getColour(ActiveTheme::SURFACE));
    button.setColour(juce::TextButton::textColourOffId, ActiveTheme::getSecondaryTextColour());
}

inline void combo(juce::ComboBox& box) {
    box.setLookAndFeel(&InspectorComboBoxLookAndFeel::getInstance());
    box.setColour(juce::ComboBox::backgroundColourId, ActiveTheme::getColour(ActiveTheme::SURFACE));
    box.setColour(juce::ComboBox::textColourId, ActiveTheme::getTextColour());
    box.setColour(juce::ComboBox::outlineColourId, ActiveTheme::getColour(ActiveTheme::BORDER));
}

}  // namespace magda::daw::ui::clip_style
