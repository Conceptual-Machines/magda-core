#pragma once

#include <juce_graphics/juce_graphics.h>

#include <algorithm>

/**
 * @brief Label placement helpers shared by the device curve views.
 *
 * Curve views draw live readouts that track a dot or a handle. Centring a
 * fixed-width box on the tracked x is fine in the middle of the plot but hangs
 * over the border once the handle nears either end, so every such box goes
 * through here instead of being positioned by hand.
 */
namespace magda::daw::ui::CurveLabelLayout {

/**
 * Centre a label box on `centreX` while keeping it inside `area`.
 *
 * As the tracked point approaches either end the box stops following it and
 * slides along the edge instead of spilling past the border. The same clamp
 * applies vertically, for labels that ride above a dot near the top of the
 * plot. A box larger than the plot shrinks to fit rather than overflowing on
 * both sides.
 *
 * @param area    Plot rectangle the label must stay inside
 * @param centreX Preferred horizontal centre (typically the dot/handle x)
 * @param y       Preferred top edge of the label box
 * @param width   Preferred label width
 * @param height  Label height
 * @return The clamped label box
 */
inline juce::Rectangle<float> centredIn(juce::Rectangle<float> area, float centreX, float y,
                                        float width, float height) {
    const float w = std::min(width, area.getWidth());
    const float h = std::min(height, area.getHeight());
    const float left = juce::jlimit(area.getX(), area.getRight() - w, centreX - (w * 0.5f));
    const float top = juce::jlimit(area.getY(), area.getBottom() - h, y);
    return {left, top, w, h};
}

/**
 * Place a label that rides above an anchor point, flipping below it when the
 * spot above is not available.
 *
 * The anchor is a dot or a handle the label belongs to. Near the top of the
 * plot there is no room above it, and for an active band the live readout
 * already owns that strip, so the label goes under the anchor instead of
 * overpainting either. The result is clamped by centredIn.
 *
 * @param area        Plot rectangle the label must stay inside
 * @param centreX     Preferred horizontal centre (the dot/handle x)
 * @param anchorY     The dot/handle centre y
 * @param width       Preferred label width
 * @param height      Label height
 * @param gap         Clearance between the anchor and the nearest label edge
 * @param reservedTop Bottom edge of a strip at the top of `area` that
 *                    something else owns; pass area.getY() when nothing is
 * @return The chosen label box
 */
inline juce::Rectangle<float> aboveAnchor(juce::Rectangle<float> area, float centreX, float anchorY,
                                          float width, float height, float gap, float reservedTop) {
    const float above = anchorY - gap - height;
    const float below = anchorY + gap;
    return centredIn(area, centreX, above >= reservedTop ? above : below, width, height);
}

}  // namespace magda::daw::ui::CurveLabelLayout
