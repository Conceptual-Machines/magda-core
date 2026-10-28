// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/**
 * @brief A layout helper for side columns (left or right)
 *
 * Provides utility methods for removing space from bounds based on side.
 */
struct SideColumn {
    bool left;

    explicit SideColumn(bool isLeft = true) : left(isLeft) {}

    // Removes width from the appropriate side of bounds and returns the removed area
    juce::Rectangle<int> removeFrom(juce::Rectangle<int>& bounds, int width) const {
        return left ? bounds.removeFromLeft(width) : bounds.removeFromRight(width);
    }

    // Removes spacing from the appropriate side of bounds
    void removeSpacing(juce::Rectangle<int>& bounds, int spacing) const {
        if (left) {
            bounds.removeFromLeft(spacing);
        } else {
            bounds.removeFromRight(spacing);
        }
    }

    // Trims from the appropriate side without returning the area
    juce::Rectangle<int> trimmed(const juce::Rectangle<int>& bounds, int amount) const {
        return left ? bounds.withTrimmedLeft(amount) : bounds.withTrimmedRight(amount);
    }
};

}  // namespace magda
