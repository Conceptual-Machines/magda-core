#pragma once

/**
 * @file TracktionAudioIO.hpp
 * @brief The Tracktion engine's audio interface, selected through its wave devices (#2748, #2749).
 */

#include <tracktion_engine/tracktion_engine.h>

#include "../audio/io/AudioIOControl.hpp"

namespace magda {

/**
 * @brief A channel is open where a wave device carrying it is enabled.
 *
 * Tracktion's wave devices expect every channel of the interface in the callback (#719), so
 * this opens them all at the JUCE level and selects through the wave devices. Goes with the
 * Tracktion engine.
 */
class TracktionAudioIO final : public AudioIOControl, private juce::ChangeListener {
  public:
    explicit TracktionAudioIO(tracktion::DeviceManager& devices);
    ~TracktionAudioIO() override;

    bool isOpen() const override;
    Direction inputs() const override;
    Direction outputs() const override;

    AudioIOSettings chosen() const override;
    juce::String apply(const AudioIOSettings& settings) override;
    juce::StringArray channelNames(const juce::String& backend, const juce::String& interfaceName,
                                   bool inputs) override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    tracktion::DeviceManager& devices_;
};

}  // namespace magda
