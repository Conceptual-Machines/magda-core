// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/DeviceInfo.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

/** @brief Open the MAGDA presets @p device can load; saving is on the floppy disk (#3234). */
void showMagdaPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                         const magda::ChainNodePath& devicePath,
                         std::function<void(const magda::DeviceInfo& liveDevice)> onLoaded,
                         std::function<void()> onDismissed);

/** @brief Ask for a name, pre-filled with the current preset's, and save a MAGDA preset. */
void showSaveMagdaPresetDialog(const magda::DeviceInfo& device,
                               const magda::ChainNodePath& devicePath,
                               std::function<void()> onSaved);

bool hasPluginPresetsAvailable(const magda::DeviceInfo& device, bool isInternalDevice);

/** @brief Open the plug-in presets and built-in programs @p device can load. */
void showPluginPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                          const magda::ChainNodePath& devicePath, bool isInternalDevice,
                          std::function<void()> onSelectionChanged,
                          std::function<void()> onDismissed);

/** @brief Ask for a name, pre-filled with the current preset's, and save a plug-in preset. */
void showSavePluginPresetDialog(const magda::DeviceInfo& device,
                                const magda::ChainNodePath& devicePath,
                                std::function<void()> onSaved);

}  // namespace magda::daw::ui
