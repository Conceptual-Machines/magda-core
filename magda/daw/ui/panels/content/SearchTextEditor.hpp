// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief The browsers' search field: magnifier glyph, Cmd+F badge and themed colours, so
 * every browser panel's search box looks and behaves the same (#3116).
 */
class SearchTextEditor : public juce::TextEditor {
  public:
    static constexpr int kHeight = 32;

    explicit SearchTextEditor(juce::String placeholder);

    /** @brief Focuses the field on Cmd+F; the owning panel calls this from keyPressed. */
    bool handleFocusShortcut(const juce::KeyPress& key);

    void mouseDown(const juce::MouseEvent& event) override;
    void paintOverChildren(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

  private:
    juce::String placeholder_;

    void applyThemeColours();
};

}  // namespace magda::daw::ui
