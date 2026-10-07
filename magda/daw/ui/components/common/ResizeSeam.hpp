#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/ActiveTheme.hpp"

namespace magda::daw::ui {

/** Paints a panel resize handle as a 1px hairline centred in its grab area,
 *  brightening while hovered or dragged. vertical is true for a handle that
 *  drags left-right, whose line runs top to bottom. */
inline void paintResizeSeam(juce::Graphics& g, juce::Component& handle, bool vertical) {
    const bool hot = handle.isMouseOverOrDragging();
    g.setColour(ActiveTheme::getColour(hot ? ActiveTheme::RESIZE_HANDLE : ActiveTheme::HAIRLINE));
    const auto bounds = handle.getLocalBounds();
    if (vertical)
        g.fillRect(bounds.getCentreX(), 0, 1, bounds.getHeight());
    else
        g.fillRect(0, bounds.getCentreY(), bounds.getWidth(), 1);
}

}  // namespace magda::daw::ui
