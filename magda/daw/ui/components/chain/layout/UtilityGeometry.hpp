// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

#include "params/ParamControlStyle.hpp"

namespace magda::daw::ui {

/**
 * @brief The Utility's body: Pan, Width, Low mono and Gain in a row over the toggles, and the
 * stereo field beside them in what width is left (#3201).
 *
 * Rows are sized for the control style. The faceplate and the param grid both place from this,
 * over the same bounds, so the field shows when the slot is widened for it.
 */
struct UtilityGeometry {
    juce::Rectangle<int> field;  ///< Empty when there is no width beside the controls.
    std::array<juce::Rectangle<int>, 4> cells;
    std::array<juce::Rectangle<int>, 5> toggles;

    static constexpr int kSpace = 8;
    static constexpr int kToggleGap = 4;
    static constexpr int kToggleHeight = 24;
    /// The controls' width: the slot's 270 less its padding.
    static constexpr int kControlsWidth = 254;
    static constexpr int kMinFieldWidth = 60;
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

    static UtilityGeometry of(juce::Rectangle<int> bounds, ParamControlStyle style) {
        UtilityGeometry g;
        auto area = bounds.reduced(kSpace);
        auto controls = area.removeFromLeft(juce::jmin(area.getWidth(), kControlsWidth));
        area.removeFromLeft(kSpace);
        if (area.getWidth() >= kMinFieldWidth)
            g.field = area;

        controls = controls.withSizeKeepingCentre(controls.getWidth(),
                                                  cellHeight(style) + kSpace + kToggleHeight);
        auto cells = controls.removeFromTop(cellHeight(style));
        controls.removeFromTop(kSpace);
        auto toggles = controls;

        const int cellWidth = (cells.getWidth() - 3 * kSpace) / 4;
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

    /** @brief Cell @p index: Pan, Width, Low mono, Gain. */
    juce::Rectangle<int> cell(int index) const {
        return cells[static_cast<size_t>(juce::jlimit(0, 3, index))];
    }
};

}  // namespace magda::daw::ui
