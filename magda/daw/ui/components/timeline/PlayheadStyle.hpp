#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/ActiveTheme.hpp"

namespace magda::daw::ui {

/** @brief The playback line, shared by the arrangement and every clip editor. */
inline void paintPlayheadLine(juce::Graphics& g, float x, float top, float bottom) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY).withAlpha(0.6f));
    g.drawLine(x, top, x, bottom, 1.5f);
}

}  // namespace magda::daw::ui
