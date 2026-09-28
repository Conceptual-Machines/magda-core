#pragma once

/**
 * @file TracktionAudioSettings.hpp
 * @brief The audio interface Tracktion saved, read for AudioIOService's first run (#2746).
 */

#include <juce_core/juce_core.h>

#include <optional>

#include "../../core/Config.hpp"

namespace magda {

/**
 * @brief What @p settingsFile says was open, or nothing if Tracktion never saved an interface.
 *
 * A channel is kept where both JUCE's stream mask and Tracktion's wave-device mask (what
 * Audio Settings toggled) had it on. AudioIOService drops bits past the interface's channels.
 */
std::optional<AudioIOSettings> readTracktionAudioSettings(const juce::File& settingsFile);

/** @brief Tracktion's Settings.xml for MAGDA, where PropertyStorage keeps it. */
juce::File tracktionSettingsFile();

}  // namespace magda
