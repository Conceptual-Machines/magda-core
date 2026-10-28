// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "BinaryData.h"

namespace magda {

/// The AI Sound Designer's glyph, wherever the feature is offered, in @p colour.
inline std::unique_ptr<juce::Drawable> makeAiSoundDesignerIcon(juce::Colour colour) {
    auto icon = juce::Drawable::createFromImageData(BinaryData::aisounddesigner_svg,
                                                    BinaryData::aisounddesigner_svgSize);
    if (icon != nullptr)
        icon->replaceColour(juce::Colour(0xFFB3B3B3), colour);
    return icon;
}

}  // namespace magda
