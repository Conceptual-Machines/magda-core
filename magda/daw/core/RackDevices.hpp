// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file RackDevices.hpp
 * @brief Racks offered as devices, so the browser's ordinary device drop can add one (#3417).
 */

#pragma once

#include <optional>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"

namespace magda {

inline constexpr const char* kRackDeviceId = "magda_rack";
inline constexpr const char* kMultibandRackDeviceId = "magda_multiband_rack";
/// Followed by the rack preset's relative name, as PresetManager::loadRackPreset takes it.
inline constexpr const char* kRackPresetDevicePrefix = "magda_rack_preset:";

/** @brief The device id that stands for the rack preset @p presetName. */
juce::String rackPresetDeviceId(const juce::String& presetName);

/** @brief Whether @p device stands for a rack rather than a device. */
bool isRackDevice(const DeviceInfo& device);

/**
 * @brief The rack @p device stands for: a new rack, a new multiband rack, or a loaded preset.
 *
 * Its ids are placeholders for the insert to re-key. Empty when @p device is not a rack device or
 * its preset does not load.
 */
std::optional<RackInfo> rackForDevice(const DeviceInfo& device);

}  // namespace magda
