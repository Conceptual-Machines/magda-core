#include "CursorManager.hpp"

namespace magda {

CursorManager& CursorManager::getInstance() {
    static CursorManager instance;
    return instance;
}

CursorManager::CursorManager() {
    zoomCursor = createZoomCursor(ZoomGlyph::None);
    zoomInCursor = createZoomCursor(ZoomGlyph::Plus);
    zoomOutCursor = createZoomCursor(ZoomGlyph::Minus);
    noteDrawCursor = createNoteDrawCursor();
}

juce::MouseCursor CursorManager::createZoomCursor(ZoomGlyph glyph) {
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    const float cx = 10.5f;
    const float cy = 10.5f;
    const float radius = 6.2f;

    juce::Path lens;
    lens.addEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    juce::Path handle;
    handle.startNewSubPath(15.0f, 15.0f);
    handle.lineTo(23.5f, 23.5f);

    const auto outlineStroke =
        juce::PathStrokeType(5.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto bodyStroke =
        juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // White outline pass for contrast on both dark and light editor regions.
    g.setColour(juce::Colours::white);
    g.strokePath(lens, outlineStroke);
    g.strokePath(handle, outlineStroke);

    g.setColour(juce::Colours::black);
    g.strokePath(lens, bodyStroke);
    g.strokePath(handle, bodyStroke);

    // Subtle inner highlight keeps the lens readable without filling it.
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawEllipse(cx - radius + 2.2f, cy - radius + 2.2f, (radius - 2.2f) * 2.0f,
                  (radius - 2.2f) * 2.0f, 1.0f);

    // Draw +/- glyph inside the lens.
    if (glyph != ZoomGlyph::None) {
        const float glyphHalf = 3.1f;
        const float glyphStroke = 1.7f;

        g.setColour(juce::Colours::black);
        g.drawLine(cx - glyphHalf, cy, cx + glyphHalf, cy, glyphStroke);

        if (glyph == ZoomGlyph::Plus) {
            g.drawLine(cx, cy - glyphHalf, cx, cy + glyphHalf, glyphStroke);
        }
    }

    // Hotspot at center of the lens
    return juce::MouseCursor(img, static_cast<int>(cx), static_cast<int>(cy));
}

juce::MouseCursor CursorManager::createNoteDrawCursor() {
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    juce::Path pencil;
    pencil.startNewSubPath(5.0f, 22.0f);
    pencil.lineTo(8.0f, 15.0f);
    pencil.lineTo(18.5f, 4.5f);
    pencil.lineTo(23.5f, 9.5f);
    pencil.lineTo(13.0f, 20.0f);
    pencil.closeSubPath();

    juce::Path tip;
    tip.startNewSubPath(5.0f, 22.0f);
    tip.lineTo(8.0f, 15.0f);
    tip.lineTo(11.0f, 18.0f);
    tip.closeSubPath();

    juce::Path bodyDivider;
    bodyDivider.startNewSubPath(16.6f, 6.4f);
    bodyDivider.lineTo(21.6f, 11.4f);

    // White outline pass for visibility on dark and light backgrounds.
    g.setColour(juce::Colours::white);
    g.strokePath(pencil, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.fillPath(tip);

    g.setColour(juce::Colours::black);
    g.strokePath(pencil, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.strokePath(bodyDivider, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    g.setColour(juce::Colour(0xFF5599FF));
    g.fillPath(tip);
    g.setColour(juce::Colours::black);
    g.strokePath(tip, juce::PathStrokeType(1.0f));

    // Hotspot at pencil tip.
    return juce::MouseCursor(img, 5, 22);
}

}  // namespace magda
