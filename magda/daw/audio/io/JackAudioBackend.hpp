#pragma once

/**
 * @file JackAudioBackend.hpp
 * @brief MAGDA's JACK backend on Linux, in place of JUCE's (#3107).
 */

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <vector>

namespace magda::jack {

/** @brief A JACK port as JACK lists it, by its full client:port name. */
struct Port {
    juce::String name;
    bool physical = false;
};

/** @brief A JACK client's audio ports in one direction, offered as one device. */
struct Device {
    juce::String name;
    juce::StringArray ports;
    bool physical = false;
};

/**
 * @brief @p ports as devices, one per client, each keeping only its physical ports when it has any,
 *        so PipeWire's monitor_* ports on a card's client aren't listed as its inputs (#3107).
 */
std::vector<Device> groupDevices(const std::vector<Port>& ports);

/**
 * @brief @p device's channels as JUCE's ALSA backend names a card's, "channel 1" onwards, in place
 *        of PipeWire's capture_FL or capture_AUX0. An app's client keeps its own port names.
 */
juce::StringArray channelNames(const Device& device);

/** @brief The JACK backend, listing audio ports grouped by groupDevices(), or nullptr off Linux. */
std::unique_ptr<juce::AudioIODeviceType> createBackend();

/** @brief Add the JACK backend to @p manager after the platform's own. */
void addBackend(juce::AudioDeviceManager& manager);

}  // namespace magda::jack
