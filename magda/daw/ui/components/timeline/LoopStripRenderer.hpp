#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/DarkTheme.hpp"

namespace magda::LoopStripRenderer {

// Loop indicator shared by every ruler (arrangement + clip editors) so the loop
// looks identical everywhere: a quiet ~38% rail between two brighter rectangular
// handles, filling [stripTop, stripTop + stripHeight]. The rectangular handles
// mirror the time-range selection handles so the two read as the same kind of
// thing (loop = green, selection = blue), and sit cleanly under the playhead's
// triangle rather than competing with it.
//
// xStart/xEnd are component-local pixel positions; anything outside
// [0, viewWidth] is skipped. `enabled` selects the bright green look vs. a
// greyed-out inactive one.
//
// This is the ONE place the loop's visual style is defined. Do not re-implement
// loop drawing in individual rulers; call this instead.
inline void draw(juce::Graphics& g, float xStart, float xEnd, int stripTop, int stripHeight,
                 int viewWidth, bool enabled) {
    if (stripHeight <= 0 || xEnd <= xStart)
        return;

    const auto base =
        enabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER) : juce::Colour(0xFF808080);
    const float top = static_cast<float>(stripTop);
    const float bottom = static_cast<float>(stripTop + stripHeight);
    const float mid = (top + bottom) * 0.5f;
    const int railH = juce::jmax(2, stripHeight / 3);
    const float railTop = mid - static_cast<float>(railH) / 2.0f;

    // Quiet rail (clamped to the visible width).
    const float x0 = juce::jmax(0.0f, xStart);
    const float x1 = juce::jmin(static_cast<float>(viewWidth), xEnd);
    if (x1 > x0) {
        g.setColour(base.withAlpha(0.38f));
        g.fillRoundedRectangle(x0, railTop, x1 - x0, static_cast<float>(railH),
                               static_cast<float>(railH) / 2.0f);
    }

    // Brighter rectangular handles at the endpoints, matching the time-range
    // selection handles.
    const auto handle = base.brighter(0.4f);
    constexpr float handleW = 4.0f;
    g.setColour(handle);
    if (xStart >= 0.0f && xStart <= static_cast<float>(viewWidth))
        g.fillRoundedRectangle(xStart - handleW / 2.0f, top, handleW, bottom - top, 1.5f);
    if (xEnd >= 0.0f && xEnd <= static_cast<float>(viewWidth))
        g.fillRoundedRectangle(xEnd - handleW / 2.0f, top, handleW, bottom - top, 1.5f);
}

}  // namespace magda::LoopStripRenderer
