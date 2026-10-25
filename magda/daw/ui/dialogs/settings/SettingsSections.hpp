#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "SettingsSection.hpp"

namespace magda {

class AudioEngine;

// Each former settings dialog builds its own sections (#3162).

/** @brief General, Appearance, Rendering, Paths, Defaults and Shortcuts. */
std::vector<std::unique_ptr<SettingsSection>> createPreferencesSections(
    juce::ApplicationCommandManager* commandManager, AudioEngine* audioEngine);
std::unique_ptr<SettingsSection> createAudioMidiSection(AudioEngine* audioEngine);
std::unique_ptr<SettingsSection> createControllersSection();
std::unique_ptr<SettingsSection> createConnectionsSection();
std::unique_ptr<SettingsSection> createPluginsSection();
std::unique_ptr<SettingsSection> createAISection();

}  // namespace magda
