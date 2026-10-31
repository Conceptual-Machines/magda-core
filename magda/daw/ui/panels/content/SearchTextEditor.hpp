// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

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

    /**
     * @brief Joins the one search term shared by every field that calls this, so typing in
     * either browser updates the other and switching keeps the term (#3217).
     */
    void shareSearchTerm();

    void mouseDown(const juce::MouseEvent& event) override;
    void paintOverChildren(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

  private:
    struct SharedTerm {
        juce::Value text;
    };

    juce::String placeholder_;
    std::unique_ptr<juce::SharedResourcePointer<SharedTerm>> sharedTerm_;

    void applyThemeColours();
};

}  // namespace magda::daw::ui
