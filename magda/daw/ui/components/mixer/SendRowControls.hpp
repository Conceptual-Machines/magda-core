// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "../common/DraggableValueLabel.hpp"
#include "core/TypeIds.hpp"

namespace magda {

/**
 * @brief A send's level: a dB box over a soft fill, with its automation state and lane menu.
 *
 * Drags write through the undo system.
 */
std::unique_ptr<DraggableValueLabel> makeSendLevelLabel(TrackId trackId, int busIndex, float level);

/** @brief Shows a linear send @p level on @p label without notifying. */
void setSendLevelLabelValue(DraggableValueLabel& label, float level);

/** @brief A send's remove button: a ghost cross that only boxes on hover. */
std::unique_ptr<juce::TextButton> makeSendRemoveButton(TrackId trackId, int busIndex);

}  // namespace magda
