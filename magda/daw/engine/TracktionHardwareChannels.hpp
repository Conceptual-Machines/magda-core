#pragma once

/**
 * @file TracktionHardwareChannels.hpp
 * @brief The Tracktion engine's hardware channels: its enabled wave devices (#2748).
 */

#include <tracktion_engine/tracktion_engine.h>

#include "../audio/io/HardwareChannels.hpp"

namespace magda {

/**
 * @brief A channel is open where a wave device carrying it is enabled.
 *
 * Tracktion opens every channel at the JUCE level, so its wave devices are the selection.
 * Goes with the Tracktion engine.
 */
class TracktionHardwareChannels final : public HardwareChannels, private juce::ChangeListener {
  public:
    explicit TracktionHardwareChannels(tracktion::DeviceManager& devices);
    ~TracktionHardwareChannels() override;

    bool isOpen() const override;
    Direction inputs() const override;
    Direction outputs() const override;

  private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    tracktion::DeviceManager& devices_;
};

}  // namespace magda
