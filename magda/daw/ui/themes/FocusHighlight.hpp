// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda::daw::ui {

/**
 * @brief Paint the accent line and inward glow of a focused panel or a selected device.
 *
 * @param glow How far the fade reaches inside the line, in pixels.
 */
void paintFocusHighlight(juce::Graphics& g, juce::Rectangle<int> bounds, float glow,
                         float cornerRadius = 0.0f);

}  // namespace magda::daw::ui
