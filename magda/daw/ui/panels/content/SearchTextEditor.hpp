#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

class SearchTextEditor : public juce::TextEditor {
  public:
    void mouseDown(const juce::MouseEvent& event) override {
        juce::TextEditor::mouseDown(event);
        if (!event.mods.isPopupMenu() && getText().isNotEmpty())
            selectAll();
    }
};

}  // namespace magda::daw::ui
