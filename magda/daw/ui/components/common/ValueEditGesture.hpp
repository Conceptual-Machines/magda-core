#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

inline bool isDirectValueEditGesture(const juce::MouseEvent& event) {
    return event.mods.isLeftButtonDown() && event.mods.isAltDown() && !event.mods.isShiftDown() &&
           !event.mods.isCommandDown() && !event.mods.isCtrlDown() && !event.mods.isPopupMenu();
}

}  // namespace magda::daw::ui
