#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/DarkTheme.hpp"

namespace magda::LoopStripRenderer {

// Endpoint-weighted loop indicator shared by every ruler (arrangement +
// clip editors) so the loop looks identical everywhere: a quiet ~38% rail
// between two bright, softly-glowing endpoint caps, centred vertically in
// [stripTop, stripTop + stripHeight].
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

    const auto green =
        enabled ? DarkTheme::getColour(DarkTheme::LOOP_MARKER) : juce::Colour(0xFF808080);
    const float cy = static_cast<float>(stripTop) + static_cast<float>(stripHeight) / 2.0f;
    const int railH = juce::jmax(2, stripHeight / 3);
    const float railTop = cy - static_cast<float>(railH) / 2.0f;

    // Quiet rail (clamped to the visible width).
    const float x0 = juce::jmax(0.0f, xStart);
    const float x1 = juce::jmin(static_cast<float>(viewWidth), xEnd);
    if (x1 > x0) {
        g.setColour(green.withAlpha(0.38f));
        g.fillRoundedRectangle(x0, railTop, x1 - x0, static_cast<float>(railH),
                               static_cast<float>(railH) / 2.0f);
    }

    // Bright glowing endpoint caps, a touch taller than the rail for weight.
    const float capH = static_cast<float>(railH) + 3.0f;
    const float capTop = cy - capH / 2.0f;
    const auto cap = [&](float x) {
        if (x < 0.0f || x > static_cast<float>(viewWidth))
            return;
        g.setColour(green.withAlpha(0.16f));
        g.fillRoundedRectangle(x - 5.0f, capTop - 1.0f, 10.0f, capH + 2.0f, 4.0f);
        g.setColour(green.withAlpha(0.34f));
        g.fillRoundedRectangle(x - 3.0f, capTop, 6.0f, capH, 3.0f);
        g.setColour(green.brighter(0.45f));
        g.fillRoundedRectangle(x - 2.0f, capTop, 4.0f, capH, 2.0f);
    };
    cap(xStart);
    cap(xEnd);
}

}  // namespace magda::LoopStripRenderer
