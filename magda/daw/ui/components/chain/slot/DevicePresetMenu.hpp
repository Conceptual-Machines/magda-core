// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/PluginPresetScanner.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

/** @brief What a loaded hosted plugin adds to its preset menu. */
struct PluginPresetSources {
    std::vector<magda::PluginPresetScanner::Entry> files;
    juce::StringArray programs;
    int currentProgram = -1;
    /// ".vstpreset" or ".aupreset"; empty for a device with no preset files of its own.
    juce::String extension;
};

/** @brief A preset menu's items, and what its preset ids resolve to. */
struct DevicePresetMenu {
    juce::PopupMenu menu;
    juce::StringArray presetPaths;
    juce::Array<juce::File> pluginFiles;
};

/**
 * @brief The menu every device's preset box opens (#3213): its MAGDA presets, then what
 *        @p plugin offers and what @p addDeviceItems adds.
 */
DevicePresetMenu buildDevicePresetMenu(const magda::DeviceInfo& device,
                                       const PluginPresetSources& plugin,
                                       const std::function<void(juce::PopupMenu&)>& addDeviceItems);

struct DevicePresetMenuCallbacks {
    /// A MAGDA preset landed on the live device.
    std::function<void(const magda::DeviceInfo& liveDevice)> onPresetLoaded;
    /// A plugin preset or program was loaded, or a plugin preset saved.
    std::function<void()> onNameChanged;
    std::function<void()> onDismissed;
    std::function<void(juce::PopupMenu&)> addDeviceItems;
};

/** @brief Opens @p device's preset menu; the floppy disk beside it saves (#3234). */
void showDevicePresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                          const magda::ChainNodePath& devicePath, bool isInternalDevice,
                          DevicePresetMenuCallbacks callbacks);

/** @brief Ask for a name, pre-filled with the current preset's, and save a MAGDA preset. */
void showSaveMagdaPresetDialog(const magda::DeviceInfo& device,
                               const magda::ChainNodePath& devicePath,
                               std::function<void()> onSaved);

}  // namespace magda::daw::ui
