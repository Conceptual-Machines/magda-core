// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

inline bool isDirectValueEditGesture(const juce::MouseEvent& event) {
    return event.mods.isLeftButtonDown() && event.mods.isAltDown() && !event.mods.isShiftDown() &&
           !event.mods.isCommandDown() && !event.mods.isCtrlDown() && !event.mods.isPopupMenu();
}

}  // namespace magda::daw::ui
