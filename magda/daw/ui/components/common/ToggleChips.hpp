// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../themes/ActiveTheme.hpp"
#include "../chain/layout/NodeHeaderStyles.hpp"
#include "SvgButton.hpp"

namespace magda::daw::ui {

/** @brief Clip loop toggle: secondary glyph at rest, a loop-green tinted chip while looping.
 *  @p glyphKey is the colour the asset draws its glyph in. */
inline void applyLoopChip(magda::SvgButton& button, juce::Colour glyphKey) {
    button.setOriginalColor(glyphKey);
    button.setNormalColor(ActiveTheme::TEXT_SECONDARY);
    button.setHoverColor(ActiveTheme::TEXT_PRIMARY);
    button.setTintedChip(ActiveTheme::LOOP_MARKER);
    button.setBorderThickness(1.0f);
    button.setClickingTogglesState(false);
}

/** @brief Clip power toggle: the device power chip, green while the clip plays. */
inline void applyPowerChip(magda::SvgButton& button, float buttonHeight) {
    node_header::applyDeviceIconStyle(button, node_header::DeviceIcon::Power,
                                      juce::Colour(0xFFE6E6E6), ActiveTheme::DEVICE_GREEN,
                                      buttonHeight);
    button.setClickingTogglesState(false);
}

}  // namespace magda::daw::ui
