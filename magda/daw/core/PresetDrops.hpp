// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file PresetDrops.hpp
 * @brief What a preset dropped on a track's chain does to it (#1329).
 */

#pragma once

#include "RackInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

/**
 * @brief Inserts @p rack, loaded from a rack preset, into @p trackId's chain at @p insertIndex.
 *
 * One undo step, with fresh ids throughout, so the same preset can be dropped twice.
 * @return false when nothing was inserted.
 */
bool insertRackPreset(TrackId trackId, RackInfo rack, int insertIndex);

}  // namespace magda
