// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SearchTextEditor.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

SearchTextEditor::SearchTextEditor(juce::String placeholder)
    : placeholder_(std::move(placeholder)) {
    setSelectAllWhenFocused(true);
    setIndents(28, 0);
    setJustification(juce::Justification::centredLeft);
    setFont(FontManager::getInstance().getUIFont(13.0f));
    applyThemeColours();
}

bool SearchTextEditor::handleFocusShortcut(const juce::KeyPress& key) {
    if (key != juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0))
        return false;
    grabKeyboardFocus();
    return true;
}

void SearchTextEditor::mouseDown(const juce::MouseEvent& event) {
    juce::TextEditor::mouseDown(event);
    if (!event.mods.isPopupMenu() && getText().isNotEmpty())
        selectAll();
}

void SearchTextEditor::paintOverChildren(juce::Graphics& g) {
    juce::TextEditor::paintOverChildren(g);

    const auto box = getLocalBounds().toFloat();
    const auto dim = ActiveTheme::getSecondaryTextColour();

    const auto icon =
        juce::Rectangle<float>(box.getX() + 10.0f, box.getCentreY() - 6.0f, 12.0f, 12.0f);
    juce::Path glass;
    glass.addEllipse(icon.getX(), icon.getY(), 9.0f, 9.0f);
    glass.startNewSubPath(icon.getX() + 7.8f, icon.getY() + 7.8f);
    glass.lineTo(icon.getRight(), icon.getBottom());
    g.setColour(dim);
    g.strokePath(glass, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    if (isEmpty() && box.getWidth() > 160.0f) {
        const auto font = FontManager::getInstance().getMonoFont(10.0f);
        const juce::String hint = juce::String::fromUTF8("\xe2\x8c\x98"
                                                         "F");
        const float w = juce::GlyphArrangement::getStringWidth(font, hint) + 10.0f;
        const auto badge =
            juce::Rectangle<float>(box.getRight() - w - 8.0f, box.getCentreY() - 8.0f, w, 16.0f);
        g.setColour(ActiveTheme::getBorderColour());
        g.drawRoundedRectangle(badge, 3.0f, 1.0f);
        g.setColour(dim);
        g.setFont(font);
        g.drawText(hint, badge, juce::Justification::centred, false);
    }
}

void SearchTextEditor::lookAndFeelChanged() {
    juce::TextEditor::lookAndFeelChanged();
    applyThemeColours();
}

void SearchTextEditor::applyThemeColours() {
    setTextToShowWhenEmpty(placeholder_, ActiveTheme::getSecondaryTextColour());
    setColour(backgroundColourId, ActiveTheme::getColour(ActiveTheme::SURFACE));
    setColour(textColourId, ActiveTheme::getTextColour());
    setColour(highlightColourId,
              ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY).withAlpha(0.45f));
    setColour(highlightedTextColourId, ActiveTheme::getTextColour());
    setColour(outlineColourId, ActiveTheme::getBorderColour());
    applyColourToAllText(ActiveTheme::getTextColour());
}

}  // namespace magda::daw::ui
