#pragma once

/**
 * @file ActiveMidiInputs.hpp
 * @brief Audio Settings' Active MIDI inputs, kept in Config rather than in a device manager.
 */

#include <juce_audio_devices/juce_audio_devices.h>

#include <set>

namespace magda {

/**
 * @brief Shows Config's active MIDI inputs as ticks in a device manager, and saves what changes.
 *
 * JUCE counts an input as ticked only once its port opens, so a port another application
 * holds shows unticked. Only a tick that moved since it was shown is saved, or such a port
 * would be switched off without anyone asking.
 */
class ActiveMidiInputs {
  public:
    /** @brief Tick in @p devices the inputs of @p available that Config has active. */
    explicit ActiveMidiInputs(
        juce::AudioDeviceManager& devices,
        juce::Array<juce::MidiDeviceInfo> available = juce::MidiInput::getAvailableDevices());

    /** @brief Save the ticks that moved since they were shown; true when any did. */
    bool saveChanges();

  private:
    std::set<juce::String> ticked() const;

    juce::AudioDeviceManager& devices_;
    juce::Array<juce::MidiDeviceInfo> available_;
    std::set<juce::String> shown_;
};

}  // namespace magda
