#pragma once

#include <juce_graphics/juce_graphics.h>

namespace magda::daw::ui {

/// The multiband dynamics' body: the selected band's knobs on the left, the graph over a row of
/// band tabs beside them, and past a divider a column of whole-device knobs. The faceplate
/// and the grid both place from this over the same bounds.
struct MultibandEditorGeometry {
    juce::Rectangle<int> plot;
    juce::Rectangle<int> tabs;
    juce::Rectangle<int> editor;
    juce::Rectangle<int> editorTitle;
    juce::Rectangle<int> knobs;
    juce::Rectangle<int> globals;

    static constexpr int kKnobWidth = 76;
    static constexpr int kKnobColumns = 5;
    static constexpr int kKnobRows = 2;
    static constexpr int kGlobalCount = 5;

    static MultibandEditorGeometry of(juce::Rectangle<int> bounds) {
        MultibandEditorGeometry geometry;
        auto area = bounds.reduced(14, 0).withTrimmedTop(10).withTrimmedBottom(14);
        geometry.globals = area.removeFromRight(kKnobWidth);
        area.removeFromRight(25);  // The divider and its margins.
        geometry.editor = area.removeFromLeft(kKnobColumns * kKnobWidth + 24);
        area.removeFromLeft(10);
        geometry.tabs = area.removeFromBottom(26);
        area.removeFromBottom(8);
        geometry.plot = area;

        auto editor = geometry.editor.reduced(12, 6);
        geometry.editorTitle = editor.removeFromTop(24);
        geometry.knobs = editor;
        return geometry;
    }

    /// Band knob @p index, five to a row, then the whole-device knobs down the column.
    juce::Rectangle<int> knob(int index) const {
        constexpr int kGap = 6;
        if (index >= kKnobColumns * kKnobRows) {
            const int row = index - kKnobColumns * kKnobRows;
            const int height = (globals.getHeight() - kGap * (kGlobalCount - 1)) / kGlobalCount;
            return globals.withHeight(height).translated(0, row * (height + kGap));
        }
        const int width = (knobs.getWidth() - kGap * (kKnobColumns - 1)) / kKnobColumns;
        const int height = (knobs.getHeight() - kGap * (kKnobRows - 1)) / kKnobRows;
        return {knobs.getX() + (index % kKnobColumns) * (width + kGap),
                knobs.getY() + (index / kKnobColumns) * (height + kGap), width, height};
    }
};

}  // namespace magda::daw::ui
