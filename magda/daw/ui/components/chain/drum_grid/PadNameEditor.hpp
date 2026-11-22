// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui {

/** @brief The field a pad's name is edited in, on its button or in its mix panel. */
inline std::unique_ptr<juce::TextEditor> makePadNameEditor(const juce::String& name,
                                                           const juce::Font& font) {
    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine(false);
    editor->setReturnKeyStartsNewLine(false);
    editor->setBorder({1, 2, 1, 2});
    editor->setIndents(3, 1);
    editor->setFont(font);
    editor->setColour(juce::TextEditor::backgroundColourId,
                      ActiveTheme::getColour(ActiveTheme::BACKGROUND));
    editor->setColour(juce::TextEditor::textColourId,
                      ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    editor->setColour(juce::TextEditor::outlineColourId,
                      ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
    editor->setColour(juce::TextEditor::focusedOutlineColourId,
                      ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
    editor->setText(name, juce::dontSendNotification);
    editor->selectAll();
    return editor;
}

}  // namespace magda::daw::ui
