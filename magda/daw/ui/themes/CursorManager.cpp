#include "CursorManager.hpp"

#include <cmath>

namespace magda {

CursorManager& CursorManager::getInstance() {
    static CursorManager instance;
    return instance;
}

CursorManager::CursorManager() {
    zoomCursor = createZoomCursor(ZoomGlyph::None);
    zoomInCursor = createZoomCursor(ZoomGlyph::Plus);
    zoomOutCursor = createZoomCursor(ZoomGlyph::Minus);
}

juce::MouseCursor CursorManager::createZoomCursor(ZoomGlyph glyph) {
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    // Lens: circle centered at (10, 10), radius 6
    const float cx = 10.0f;
    const float cy = 10.0f;
    const float radius = 6.0f;
    const float stroke = 3.4f;

    // Handle: line from lens edge at 45 degrees to corner
    float angle = juce::MathConstants<float>::pi * 0.25f;  // 45 degrees
    float handleStartX = cx + (radius + 0.5f) * std::cos(angle);
    float handleStartY = cy + (radius + 0.5f) * std::sin(angle);
    float handleEndX = 24.0f;
    float handleEndY = 24.0f;

    // White outline pass (thicker, drawn first)
    g.setColour(juce::Colours::white);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, stroke + 2.2f);
    g.drawLine(handleStartX, handleStartY, handleEndX, handleEndY, stroke + 2.6f);

    // Black foreground stroke (ring only, transparent center)
    g.setColour(juce::Colours::black);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, stroke);
    g.drawLine(handleStartX, handleStartY, handleEndX, handleEndY, stroke);

    // Draw +/- glyph inside the lens (white on black)
    if (glyph != ZoomGlyph::None) {
        const float glyphHalf = 3.0f;
        const float glyphStroke = 1.4f;

        // Horizontal bar (both + and -)
        g.setColour(juce::Colours::white);
        g.drawLine(cx - glyphHalf, cy, cx + glyphHalf, cy, glyphStroke);

        if (glyph == ZoomGlyph::Plus) {
            // Vertical bar
            g.drawLine(cx, cy - glyphHalf, cx, cy + glyphHalf, glyphStroke);
        }
    }

    // Hotspot at center of the lens
    return juce::MouseCursor(img, static_cast<int>(cx), static_cast<int>(cy));
}

}  // namespace magda
