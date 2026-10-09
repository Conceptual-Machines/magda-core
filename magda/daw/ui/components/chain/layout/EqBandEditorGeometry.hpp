#pragma once

#include <juce_graphics/juce_graphics.h>

namespace magda::daw::ui {

/// The EQ's one-band-at-a-time body: the curve, a row of band chips, and an editor for the
/// selected band whose knobs sit at its right. The faceplate and the grid both place from
/// this, over the same bounds, so the knobs land in the editor the faceplate draws.
struct EqBandEditorGeometry {
    juce::Rectangle<int> plot;
    juce::Rectangle<int> chips;
    juce::Rectangle<int> editor;
    juce::Rectangle<int> editorControls;  // The editor's left: switch, name, type.
    juce::Rectangle<int> knobs;           // The selected band's three knobs.
    juce::Rectangle<int> output;          // Output, past a divider.

    static constexpr int kKnobWidth = 76;

    static EqBandEditorGeometry of(juce::Rectangle<int> bounds) {
        EqBandEditorGeometry geometry;
        auto area = bounds.reduced(14, 0).withTrimmedTop(10).withTrimmedBottom(14);
        geometry.editor = area.removeFromBottom(96);
        area.removeFromBottom(8);
        geometry.chips = area.removeFromBottom(26);
        area.removeFromBottom(8);
        geometry.plot = area;

        auto editor = geometry.editor.reduced(12, 6);
        geometry.output = editor.removeFromRight(kKnobWidth);
        editor.removeFromRight(25);  // The divider and its margins.
        geometry.knobs = editor.removeFromRight(3 * kKnobWidth);
        geometry.editorControls = editor;
        return geometry;
    }

    /// Knob @p index: the band's three, then Output.
    juce::Rectangle<int> knob(int index) const {
        if (index >= 3)
            return output;
        return knobs.withWidth(kKnobWidth).translated(index * kKnobWidth, 0);
    }
};

}  // namespace magda::daw::ui
