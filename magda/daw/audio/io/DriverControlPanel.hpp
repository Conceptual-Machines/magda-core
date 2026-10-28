// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file DriverControlPanel.hpp
 * @brief A driver's own settings window, as ASIO drivers have (#3091).
 */

#include <juce_audio_devices/juce_audio_devices.h>

namespace magda {

/**
 * @brief Show @p device's control panel, with MAGDA's windows blocked on Windows as
 *        juce::AudioDeviceSelectorComponent does.
 *
 * @return Whether the driver changed something the interface must be reopened for.
 */
bool showDriverControlPanel(juce::AudioIODevice& device);

}  // namespace magda
