// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include "core/ChainNodePath.hpp"

namespace magda::faust_edits {

/**
 * @brief Make @p source the patch of the runtime Faust device at @p devicePath, on the model.
 *
 * The source is compiled on a detached device first, so a failure leaves the model untouched and
 * @p error says why. On success the model takes the source together with the parameters, meters
 * and sidechain port the patch declares, and the engine follows it (#2317, #2659).
 */
bool loadSource(const ChainNodePath& devicePath, const juce::String& name,
                const juce::String& source, juce::String& error);

/**
 * @brief Declares the patch a preset just put on the model at @p devicePath, when it isn't the one
 *        @p previousState held (#3213).
 *
 * A preset's state carries the source but not the meters and sidechain port it declares.
 */
void adoptPresetPatch(const ChainNodePath& devicePath, const juce::String& previousState);

}  // namespace magda::faust_edits
