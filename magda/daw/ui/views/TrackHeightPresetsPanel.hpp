// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace magda {

/** @brief Opens the compact / medium / large track heights for editing over @p screenArea. */
void showTrackHeightPresets(juce::Rectangle<int> screenArea);

/** @brief Opens the narrow / medium / wide track widths for editing over @p screenArea. */
void showTrackWidthPresets(juce::Rectangle<int> screenArea);

}  // namespace magda
