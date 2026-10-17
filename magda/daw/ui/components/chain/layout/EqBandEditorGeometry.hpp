#pragma once

#include <juce_graphics/juce_graphics.h>

namespace magda::daw::ui {

/**
 * @brief The EQ's one-band-at-a-time body: the curve, a row of band chips, and an editor for
 * the selected band.
 *
 * One spacing unit sets the gutter, the gaps between rows and cells, and the editor's padding.
 * The faceplate and the param grid both place from this, over the same bounds.
 */
struct EqBandEditorGeometry {
    juce::Rectangle<int> plot;
    juce::Rectangle<int> chips;
    juce::Rectangle<int> editor;
    juce::Rectangle<int> types;
    juce::Rectangle<int> slope;  ///< 12 / 24 dB/oct, after the types.
    juce::Rectangle<int> note;   ///< The note tile, before Freq.
    juce::Rectangle<int> noteLabel;
    juce::Rectangle<int> noteValue;
    juce::Rectangle<int> knobs;            ///< Freq, Gain and Q.
    juce::Rectangle<int> divider;          ///< The 1px rule before Output.
    juce::Rectangle<int> controlsDivider;  ///< The 1px rule between the filter and its values.
    juce::Rectangle<int> output;

    static constexpr int kSpace = 8;
    static constexpr int kChipHeight = 26;
    static constexpr int kCellWidth = 72;
    static constexpr int kCellHeight = 48;  ///< ParamSlotComponent's text tile at its minimum.
    static constexpr int kEditorHeight = kCellHeight + 2 * kSpace;
    static constexpr int kTypesHeight = 28;

    /** @brief Lays out @p bounds, with the type and slope strips at their preferred widths. */
    static EqBandEditorGeometry of(juce::Rectangle<int> bounds, int typesWidth = 0,
                                   int slopeWidth = 0) {
        EqBandEditorGeometry g;
        auto area = bounds.reduced(kSpace);
        g.editor = area.removeFromBottom(kEditorHeight);
        area.removeFromBottom(kSpace);
        g.chips = area.removeFromBottom(kChipHeight);
        area.removeFromBottom(kSpace);
        g.plot = area;

        auto row = g.editor.reduced(kSpace);
        const auto cell = [&row] { return row.removeFromRight(kCellWidth); };
        g.output = cell();
        g.divider = row.removeFromRight(2 * kSpace + 1).withSizeKeepingCentre(1, kCellHeight);
        g.knobs = row.removeFromRight(3 * kCellWidth + 2 * kSpace);
        row.removeFromRight(kSpace);
        g.note = cell();
        g.controlsDivider =
            row.removeFromRight(2 * kSpace + 1).withSizeKeepingCentre(1, kCellHeight);
        // As ParamSlotComponent's text tile: label on top, value, then its mod row.
        auto tile = g.note.reduced(8, 5);
        tile.removeFromBottom(7 + 2);
        g.noteLabel = tile.removeFromTop(13);
        g.noteValue = tile;

        const auto strip = [&row](int width) {
            width = juce::jmin(width, row.getWidth());
            return row.removeFromLeft(width).withSizeKeepingCentre(width, kTypesHeight);
        };
        g.types = strip(typesWidth);
        row.removeFromLeft(kSpace);
        g.slope = strip(slopeWidth);
        return g;
    }

    /** @brief Knob @p index: Freq, Gain and Q, then Output. */
    juce::Rectangle<int> knob(int index) const {
        if (index >= 3)
            return output;
        return knobs.withWidth(kCellWidth).translated(index * (kCellWidth + kSpace), 0);
    }
};

}  // namespace magda::daw::ui
