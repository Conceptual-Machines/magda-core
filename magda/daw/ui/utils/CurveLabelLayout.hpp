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

}  // namespace magda::daw::ui::CurveLabelLayout
