// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <set>
#include <vector>

#include "core/TrackInfo.hpp"

namespace magda {
class ClipManager;
class SelectionManager;
}  // namespace magda

namespace magda::daw::engine_host {

/**
 * @brief Tracks someone may be playing now (#1898): armed, monitoring, or holding the selected
 *        track, clip, notes or device.
 *
 * The instruments among them render at the callback while every other one renders ahead.
 */
std::set<TrackId> tracksBeingPlayed(const std::vector<TrackInfo>& tracks,
                                    const SelectionManager& selection, const ClipManager& clips);

}  // namespace magda::daw::engine_host
