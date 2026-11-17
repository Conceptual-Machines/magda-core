// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief Whether some of @p component lies inside every ancestor's bounds.
 *
 * isShowing() is true for a mixer strip scrolled out of its viewport; this is false
 * for it, so per-frame work can follow what is actually on screen.
 */
inline bool isScrolledIntoView(const juce::Component& component) {
    if (!component.isShowing())
        return false;

    auto area = component.getLocalBounds();
    const juce::Component* child = &component;
    for (const auto* parent = component.getParentComponent(); parent != nullptr;
         child = parent, parent = parent->getParentComponent()) {
        area = (area + child->getPosition()).getIntersection(parent->getLocalBounds());
        if (area.isEmpty())
            return false;
    }
    return true;
}

}  // namespace magda::daw::ui
