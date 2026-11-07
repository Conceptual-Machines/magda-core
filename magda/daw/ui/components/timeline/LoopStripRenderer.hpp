// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/ActiveTheme.hpp"

namespace magda::LoopStripRenderer {

/**
 * @brief Draws the loop as a thin top line with a wedge hanging from each end, filling
 * [stripTop, stripTop + stripHeight].
 *
 * Every ruler (arrangement and clip editors) calls this, so the loop's look lives here only.
 * @p xStart and @p xEnd are component-local pixels; a disabled loop draws grey.
 */
inline void draw(juce::Graphics& g, float xStart, float xEnd, int stripTop, int stripHeight,
                 int viewWidth, bool enabled) {
    const auto width = static_cast<float>(viewWidth);
    if (stripHeight <= 0 || xEnd <= xStart || xEnd < 0.0f || xStart > width)
        return;

    const auto base =
        enabled ? ActiveTheme::getColour(ActiveTheme::LOOP_MARKER) : juce::Colour(0xFF808080);
    const auto top = static_cast<float>(stripTop);
    const auto bottom = static_cast<float>(stripTop + stripHeight);
    const float barBottom = top + juce::jmin(2.0f, bottom - top);
    // 45-degree wedges, narrowed so both fit inside a short loop.
    const float wedgeW = juce::jmin(bottom - barBottom, (xEnd - xStart) * 0.5f);

    // An end far off-screen is pulled in to just past the edge, so extreme zoom never
    // hands the rasteriser huge coordinates.
    const float margin = wedgeW + 2.0f;
    const bool leftWedge = xStart >= -margin;
    const bool rightWedge = xEnd <= width + margin;
    const float x0 = leftWedge ? xStart : -margin;
    const float x1 = rightWedge ? xEnd : width + margin;

    juce::Path shape;
    shape.startNewSubPath(x0, top);
    shape.lineTo(x1, top);
    if (rightWedge) {
        shape.lineTo(x1, bottom);
        shape.lineTo(x1 - wedgeW, barBottom);
    } else {
        shape.lineTo(x1, barBottom);
    }
    if (leftWedge) {
        shape.lineTo(x0 + wedgeW, barBottom);
        shape.lineTo(x0, bottom);
    } else {
        shape.lineTo(x0, barBottom);
    }
    shape.closeSubPath();

    g.setColour(base.brighter(0.4f));
    g.fillPath(shape);
}

}  // namespace magda::LoopStripRenderer
