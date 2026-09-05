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
    eraseCursor = createEraseCursor();
    noteRepeatCursor = createNoteRepeatCursor();
    bladeCursor = createBladeCursor();
    ghostCopyCursor = createGhostCopyCursor();
    curveBendCursor = createCurveBendCursor();
}

juce::MouseCursor CursorManager::createCurveBendCursor() {
    // Bending a glide segment (#2198): an S-curve with a small double-headed
    // arrow standing clear of it. The curve says what is about to change, the
    // arrow says the drag is vertical.
    //
    // Kept apart rather than overlaid. At 28px with the outline pass every
    // other tool cursor here uses, two strokes crossing each other merge into
    // one unreadable mark -- the first attempt put the arrow through the
    // curve's midpoint and neither shape survived.
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    // Low on the left, high on the right, the way a rising glide is drawn.
    juce::Path curve;
    curve.startNewSubPath(3.0f, 24.0f);
    curve.cubicTo(12.0f, 24.0f, 14.0f, 11.0f, 25.0f, 11.0f);

    juce::Path arrow;
    arrow.startNewSubPath(7.0f, 3.0f);
    arrow.lineTo(7.0f, 13.0f);
    arrow.startNewSubPath(4.4f, 5.6f);
    arrow.lineTo(7.0f, 2.6f);
    arrow.lineTo(9.6f, 5.6f);
    arrow.startNewSubPath(4.4f, 10.4f);
    arrow.lineTo(7.0f, 13.4f);
    arrow.lineTo(9.6f, 10.4f);

    // White outline pass then the body, as the other tool cursors do, so it
    // stays readable over both the dark grid and a bright clip colour.
    const auto pass = [&g](const juce::Path& path, float outline, float body) {
        g.setColour(juce::Colours::white);
        g.strokePath(path, juce::PathStrokeType(outline, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        g.setColour(juce::Colours::black);
        g.strokePath(path, juce::PathStrokeType(body, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    };
    pass(curve, 4.0f, 2.0f);
    pass(arrow, 3.4f, 1.6f);

    // Hotspot on the curve's own midpoint, which is the thing being grabbed.
    return {img, 13, 17};
}

juce::MouseCursor CursorManager::createGhostCopyCursor() {
    // Ghost-copy drag (Alt+Shift): two interlocked chain links on the 45°
    // diagonal — the same link vocabulary as the ghost-clip header glyph.
    // White outline + black body for contrast, like the other tool cursors.
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    juce::Path link;
    link.addRoundedRectangle(-6.0f, -3.5f, 12.0f, 7.0f, 3.5f);

    const auto place = [](float cx, float cy) {
        return juce::AffineTransform::rotation(-juce::MathConstants<float>::pi / 4.0f)
            .translated(cx, cy);
    };

    juce::Path links;
    links.addPath(link, place(10.5f, 17.5f));
    links.addPath(link, place(17.5f, 10.5f));

    const auto outlineStroke =
        juce::PathStrokeType(4.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto bodyStroke =
        juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    g.setColour(juce::Colours::white);
    g.strokePath(links, outlineStroke);
    g.setColour(juce::Colours::black);
    g.strokePath(links, bodyStroke);

    // Hotspot at the joint between the two links (glyph centre).
    return {img, 14, 14};
}

juce::MouseCursor CursorManager::createBladeCursor() {
    // Scissors glyph: two crossed blades pivoting at the centre, finger rings
    // at the top. Hotspot sits at the blade tips (bottom centre) so the cut
    // lands where the user points.
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    juce::Path blades;
    // Left ring -> right blade tip
    blades.startNewSubPath(9.5f, 7.0f);
    blades.lineTo(18.5f, 23.0f);
    // Right ring -> left blade tip
    blades.startNewSubPath(18.5f, 7.0f);
    blades.lineTo(9.5f, 23.0f);

    juce::Path rings;
    rings.addEllipse(5.5f, 2.0f, 6.0f, 6.0f);
    rings.addEllipse(16.5f, 2.0f, 6.0f, 6.0f);

    const auto outlineStroke =
        juce::PathStrokeType(4.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto bodyStroke =
        juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // White outline pass for contrast on both dark and light regions
    g.setColour(juce::Colours::white);
    g.strokePath(blades, outlineStroke);
    g.strokePath(rings, outlineStroke);

    g.setColour(juce::Colours::black);
    g.strokePath(blades, bodyStroke);
    g.strokePath(rings, bodyStroke);

    // Pivot screw
    g.setColour(juce::Colours::white);
    g.fillEllipse(12.5f, 13.5f, 3.0f, 3.0f);
    g.setColour(juce::Colours::black);
    g.fillEllipse(13.25f, 14.25f, 1.5f, 1.5f);

    // Hotspot at the blade tips (bottom centre)
    return {img, 14, 23};
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
    return {img, static_cast<int>(cx), static_cast<int>(cy)};
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
    return {img, 5, 22};
}

juce::MouseCursor CursorManager::createEraseCursor() {
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    juce::Path eraser;
    eraser.startNewSubPath(6.0f, 18.5f);
    eraser.lineTo(15.5f, 9.0f);
    eraser.lineTo(23.0f, 16.5f);
    eraser.lineTo(15.0f, 24.5f);
    eraser.lineTo(9.0f, 24.5f);
    eraser.closeSubPath();

    juce::Path cut;
    cut.startNewSubPath(12.0f, 12.5f);
    cut.lineTo(19.5f, 20.0f);

    g.setColour(juce::Colours::white);
    g.strokePath(eraser, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    g.setColour(juce::Colour(0xFFFF6666));
    g.fillPath(eraser);
    g.setColour(juce::Colours::black);
    g.strokePath(eraser, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.strokePath(cut, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    g.setColour(juce::Colours::white);
    g.drawLine(5.0f, 5.0f, 12.0f, 12.0f, 4.2f);
    g.drawLine(12.0f, 5.0f, 5.0f, 12.0f, 4.2f);
    g.setColour(juce::Colours::black);
    g.drawLine(5.0f, 5.0f, 12.0f, 12.0f, 2.0f);
    g.drawLine(12.0f, 5.0f, 5.0f, 12.0f, 2.0f);

    return {img, 8, 20};
}

juce::MouseCursor CursorManager::createNoteRepeatCursor() {
    const int size = 28;
    juce::Image img(juce::Image::ARGB, size, size, true);
    juce::Graphics g(img);

    // Three small drum cells trailing right, suggesting a single stamp that
    // repeats. Hotspot sits on the leftmost cell (the origin of the repeat).
    auto makeCell = [](float x, float y, float w, float h) {
        juce::Path p;
        p.addRoundedRectangle(x, y, w, h, 1.2f);
        return p;
    };

    juce::Path c1 = makeCell(5.0f, 10.5f, 5.0f, 7.0f);
    juce::Path c2 = makeCell(12.0f, 10.5f, 5.0f, 7.0f);
    juce::Path c3 = makeCell(19.0f, 10.5f, 5.0f, 7.0f);

    // White outline pass for visibility on dark and light backgrounds.
    const auto outlineStroke =
        juce::PathStrokeType(3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour(juce::Colours::white);
    g.strokePath(c1, outlineStroke);
    g.strokePath(c2, outlineStroke);
    g.strokePath(c3, outlineStroke);

    // Fill in a warm accent so the repeater reads as a third distinct tool
    // alongside the blue pencil and the red eraser. STATUS_WARNING orange
    // from the theme palette.
    g.setColour(juce::Colour(0xFFFFAA44));
    g.fillPath(c1);
    g.fillPath(c2);
    g.fillPath(c3);

    // Crisp black hairline outline.
    g.setColour(juce::Colours::black);
    const auto hairline =
        juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.strokePath(c1, hairline);
    g.strokePath(c2, hairline);
    g.strokePath(c3, hairline);

    // Hotspot at the centre of the leftmost cell — the click point lands there.
    return {img, 7, 14};
}

}  // namespace magda
