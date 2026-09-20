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

    juce::StringArray backendNames() override;
    juce::StringArray interfaceNames(const juce::String& backend, bool inputs) override;
    bool isSingleInterfaceBackend(const juce::String& backend) override;
    std::vector<double> availableSampleRates() const override;
    std::vector<int> availableBufferSizes() const override;

    void addCallback(juce::AudioIODeviceCallback* callback) override;
    void removeCallback(juce::AudioIODeviceCallback* callback) override;
    Status status() const override;

  private:
    /// The JUCE manager behind Tracktion's own, which is what the interfaces are listed off.
    juce::AudioDeviceManager& manager() const;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    tracktion::DeviceManager& devices_;
};

}  // namespace magda
