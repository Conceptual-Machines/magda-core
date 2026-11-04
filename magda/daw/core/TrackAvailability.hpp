// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <set>
#include <vector>

#include "DeviceInfo.hpp"
#include "TrackInfo.hpp"

/**
 * @file
 * @brief Tracks held off by a device this build does not register (#3101).
 *
 * Derived from the chain each time it is asked rather than stored, so the
 * project keeps the track's own enable and the device's saved state, and
 * removing or replacing the device lifts the hold with no step of its own.
 */

namespace magda {

/// A MAGDA device whose id neither catalog in this build knows. An empty id is
/// an unset slot rather than a device.
bool isUnregisteredDevice(const DeviceInfo& device);

/// The devices on @p track this build does not register, in chain order, racks
/// and pads included.
std::vector<const DeviceInfo*> unregisteredDevicesOn(const TrackInfo& track);

/// "<names> is not available in this build", worded for one or several. Empty for none.
juce::String unavailableDevicesText(const juce::StringArray& names);

/// The tracks among @p tracks and @p master holding such a device.
std::set<TrackId> tracksHoldingUnregisteredDevices(const std::vector<TrackInfo>& tracks,
                                                   const TrackInfo& master);

}  // namespace magda
