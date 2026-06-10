#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * @brief Unified multi-selection modifier policy.
 *
 * Every selectable surface (tracks, clips, session slots, chain nodes,
 * mixer strips, notes) resolves click modifiers through these helpers so
 * the gesture language stays consistent app-wide:
 *
 *  - plain click          -> replace the selection
 *  - Cmd (Ctrl on Win)    -> toggle the clicked item ("toggle select")
 *  - Shift                -> extend a contiguous range from the anchor
 *                            ("range select")
 *  - Cmd + marquee drag   -> add the marquee contents to the selection
 *
 * Shift+drag gestures that start on an item (duplicate, stretch) are
 * unaffected: range select fires on click, drags are disambiguated by the
 * usual movement threshold. Shift+Ctrl stays reserved for the erase
 * gesture, which is why range select excludes Ctrl.
 */
inline bool isToggleSelectClick(const juce::ModifierKeys& mods) {
    return mods.isCommandDown() && !mods.isShiftDown();
}

inline bool isRangeSelectClick(const juce::ModifierKeys& mods) {
    return mods.isShiftDown() && !mods.isCommandDown() && !mods.isCtrlDown();
}

inline bool isAdditiveMarqueeDrag(const juce::ModifierKeys& mods) {
    return mods.isCommandDown();
}

}  // namespace magda
