#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
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

class MagdaDevicePresetPresenter {
  public:
    MagdaDevicePresetPresenter();

    void clearCurrentPreset();
    void showMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                  const magda::ChainNodePath& devicePath,
                  std::function<void(const magda::DeviceInfo& liveDevice)> onLoaded);

  private:
    struct State;
    std::shared_ptr<State> state_;
};

class PluginDevicePresetPresenter {
  public:
    PluginDevicePresetPresenter();

    void clearCurrentPreset();
    juce::String getCurrentPresetLabel() const;
    void showMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                  const magda::ChainNodePath& devicePath, bool isInternalDevice,
                  std::function<void()> onSelectionChanged);
    void loadFile(const magda::ChainNodePath& devicePath, const juce::File& file,
                  std::function<void()> onSelectionChanged);
    void showSaveDialog(const magda::DeviceInfo& device, const magda::ChainNodePath& devicePath,
                        std::function<void()> onSelectionChanged);

  private:
    struct State;
    std::shared_ptr<State> state_;
};

bool hasPluginPresetsAvailable(const magda::DeviceInfo& device, bool isInternalDevice);

juce::LookAndFeel& getPluginPresetsButtonLookAndFeel();

struct PluginPresetMenuActions {
    std::function<void()> saveAs;
    std::function<void(const juce::File& file)> loadFile;
    std::function<void(const juce::File& currentFile, const juce::String& displayName)>
        selectionChanged;
};

void showPluginPresetMenu(juce::Component* targetComponent, const magda::DeviceInfo& device,
                          const magda::ChainNodePath& devicePath, bool isInternalDevice,
                          const juce::File& currentPluginPresetFile,
                          PluginPresetMenuActions actions);

void loadPluginPresetFile(
    const magda::ChainNodePath& devicePath, const juce::File& file,
    std::function<void(const juce::File& currentFile, const juce::String& displayName)> onLoaded);

void showSavePluginPresetDialog(
    const magda::DeviceInfo& device, const magda::ChainNodePath& devicePath,
    const juce::String& currentPluginPresetName,
    std::function<void(const juce::File& currentFile, const juce::String& displayName)> onSaved);

}  // namespace magda::daw::ui
