#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "core/DeviceInfo.hpp"
#include "core/TypeIds.hpp"

namespace magda::daw::ui {

using PresetSnapshotProvider = std::function<std::optional<magda::DeviceInfo>()>;

struct MagdaPresetMenuActions {
    std::function<void()> saveAs;
    std::function<void()> saveCurrent;
    std::function<void(const juce::String& presetRelativePath)> loadPreset;
};

void showMagdaPresetMenu(juce::Component* targetComponent, const juce::String& pluginFolder,
                         const juce::String& currentPresetName, MagdaPresetMenuActions actions);

std::optional<magda::DeviceInfo> snapshotDeviceForPreset(const magda::DeviceInfo& fallbackDevice,
                                                         const magda::ChainNodePath& nodePath);

void showSaveMagdaPresetDialog(const magda::DeviceInfo& device,
                               const juce::String& currentPresetName,
                               PresetSnapshotProvider snapshotProvider,
                               std::function<void(const juce::String& presetName)> onSaved);

void saveCurrentMagdaPreset(const juce::String& currentPresetName,
                            PresetSnapshotProvider snapshotProvider);

void loadMagdaPreset(
    const juce::String& pluginFolder, const magda::ChainNodePath& nodePath,
    const juce::String& presetRelativePath,
    std::function<void(const magda::DeviceInfo& liveDevice, const juce::String& presetName)>
        onLoaded);

bool hasPluginPresetsAvailable(const magda::DeviceInfo& device, bool isInternalDevice);

struct PluginPresetMenuActions {
    std::function<void()> saveAs;
    std::function<void(const juce::File& file)> loadFile;
    std::function<void(const juce::File& currentFile, const juce::String& displayName)>
        selectionChanged;
};

void showPluginPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                          bool isInternalDevice, const juce::File& currentPluginPresetFile,
                          PluginPresetMenuActions actions);

void loadPluginPresetFile(
    magda::DeviceId deviceId, const juce::File& file,
    std::function<void(const juce::File& currentFile, const juce::String& displayName)> onLoaded);

void showSavePluginPresetDialog(
    const magda::DeviceInfo& device, const juce::String& currentPluginPresetName,
    std::function<void(const juce::File& currentFile, const juce::String& displayName)> onSaved);

}  // namespace magda::daw::ui
