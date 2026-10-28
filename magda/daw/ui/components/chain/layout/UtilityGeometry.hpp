// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

#include "params/ParamControlStyle.hpp"

namespace magda::daw::ui {

/**
 * @brief The Utility's body: the stereo field on top, then Pan, Width and Low mono, the toggles,
 * and Gain across the foot in a cell of its own.
 *
 * Rows are sized for the control style, so Gain's knob draws larger than the three above it.
 * The faceplate and the param grid both place from this, over the same bounds.
 */
struct UtilityGeometry {
    juce::Rectangle<int> field;  ///< Empty when the body is too short to draw it.
    std::array<juce::Rectangle<int>, 3> cells;
    std::array<juce::Rectangle<int>, 5> toggles;
    juce::Rectangle<int> gain;

    static constexpr int kSpace = 8;
    static constexpr int kToggleGap = 4;
    static constexpr int kToggleHeight = 24;
    static constexpr int kMinFieldHeight = 24;
    /// Low mono's label is the long one; the others share what is left.
    static constexpr float kLowMonoToggleWeight = 1.7f;

    static int cellHeight(ParamControlStyle style) {
        switch (style) {
            case ParamControlStyle::Knobs:
                return 72;
            case ParamControlStyle::Sliders:
                return 44;
            case ParamControlStyle::Text:
                break;
        }
        return 48;  // ParamSlotComponent's text tile at its minimum.
    }

    static int gainHeight(ParamControlStyle style) {
        return style == ParamControlStyle::Knobs ? 88 : cellHeight(style);
    }

    static UtilityGeometry of(juce::Rectangle<int> bounds, ParamControlStyle style) {
        UtilityGeometry g;
        auto area = bounds.reduced(kSpace);
        g.gain = area.removeFromBottom(gainHeight(style));
        area.removeFromBottom(kSpace);
        auto toggles = area.removeFromBottom(kToggleHeight);
        area.removeFromBottom(kSpace);
        auto cells = area.removeFromBottom(cellHeight(style));
        area.removeFromBottom(kSpace);
        if (area.getHeight() >= kMinFieldHeight)
            g.field = area;

        const int cellWidth = (cells.getWidth() - 2 * kSpace) / 3;
        for (auto& cell : g.cells) {
            cell = cells.removeFromLeft(&cell == &g.cells.back() ? cells.getWidth() : cellWidth);
            cells.removeFromLeft(kSpace);
        }

        const float unit =
            static_cast<float>(toggles.getWidth() - 4 * kToggleGap) / (4.0f + kLowMonoToggleWeight);
        for (size_t i = 0; i < g.toggles.size(); ++i) {
            const float weight = i == 1 ? kLowMonoToggleWeight : 1.0f;
            const bool last = i + 1 == g.toggles.size();
            g.toggles[i] =
                toggles.removeFromLeft(last ? toggles.getWidth() : juce::roundToInt(unit * weight));
            toggles.removeFromLeft(kToggleGap);
        }
        return g;
    }

    /** @brief Cell @p index: Pan, Width and Low mono, then Gain. */
    juce::Rectangle<int> cell(int index) const {
        return index >= 3 ? gain : cells[static_cast<size_t>(index)];
    }
};

}  // namespace magda::daw::ui
