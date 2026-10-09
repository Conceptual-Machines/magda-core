#pragma once

#include <juce_graphics/juce_graphics.h>

namespace magda::daw::ui {

/// A device whose faceplate sits under its band knobs: a column of global knobs on the left,
/// band knobs in equal columns along the top right, the faceplate below them. The grid and
/// the slot both place from this, so the cells and the faceplate cannot drift apart.
struct FaceplateBelowGeometry {
    juce::Rectangle<int> globals;
    juce::Rectangle<int> bands;
    juce::Rectangle<int> faceplate;

    static constexpr int kColumnWidth = 84;  // One 64px cell and the grid's 10px padding.

    static FaceplateBelowGeometry of(juce::Rectangle<int> content, bool faceplateShown,
                                     int bandRows) {
        FaceplateBelowGeometry geometry;
        geometry.globals = content.removeFromLeft(kColumnWidth);
        if (!faceplateShown) {
            geometry.bands = content;
            return geometry;
        }
        const int bandHeight = juce::jmin(content.getHeight() / 2, bandRows * 72 + 16);
        geometry.bands = content.removeFromTop(bandHeight);
        geometry.faceplate = content.withTrimmedRight(14).withTrimmedBottom(14);
        return geometry;
    }

    /// Cell @p index of @p rows stacked in @p area, or of @p columns across it.
    static juce::Rectangle<int> cell(juce::Rectangle<int> area, int index, int columns, int rows) {
        constexpr int kGap = 6;
        area = area.reduced(10, 8);
        const int width = (area.getWidth() - kGap * (columns - 1)) / juce::jmax(1, columns);
        const int height = (area.getHeight() - kGap * (rows - 1)) / juce::jmax(1, rows);
        return {area.getX() + (index % columns) * (width + kGap),
                area.getY() + (index / columns) * (height + kGap), width, height};
    }
};

}  // namespace magda::daw::ui
