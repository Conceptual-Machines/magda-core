#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace magda {

/**
 * Returns the audio driver type whose devices should be listed in the UI: the
 * one the device manager is currently using, falling back to the first available
 * type, or nullptr if there are none.
 *
 * Listing from getAvailableDeviceTypes()[0] unconditionally was a Mac-first bug:
 * macOS has a single type (CoreAudio) so [0] is always the active one, but
 * Windows (Windows Audio / DirectSound / ASIO) and Linux (ALSA / JACK) have
 * several. Once a non-first driver was selected, [0] listed the wrong driver's
 * devices and applying one failed with "No such device". The active type is the
 * source of truth.
 */
inline juce::AudioIODeviceType* activeDeviceTypeFor(juce::AudioDeviceManager& deviceManager) {
    if (auto* current = deviceManager.getCurrentDeviceTypeObject())
        return current;

    auto& types = deviceManager.getAvailableDeviceTypes();
    return types.isEmpty() ? nullptr : types.getFirst();
}

}  // namespace magda
