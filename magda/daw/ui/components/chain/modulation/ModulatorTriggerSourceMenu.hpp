// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/ChainNodePath.hpp"

namespace magda::daw::ui {

/**
 * @brief The modulator editor's Advanced menu: where a device's or rack's audio or MIDI
 * trigger comes from, itself or another track.
 *
 * Audio for an envelope follower or a level-triggered mod, MIDI otherwise. Does nothing for
 * any other owner.
 */
void showModulatorTriggerSourceMenu(const magda::ChainNodePath& ownerPath, int modIndex);

}  // namespace magda::daw::ui
