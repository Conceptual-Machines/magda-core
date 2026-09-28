#pragma once

#include <utility>
#include <vector>

#include "audio/plugins/MagdaDevice.hpp"

namespace magda::test {

// Growable test storage for either side of the SDK's MIDI contract: fill
// `events` to feed a device, or hand an empty one over as the device's output.
class DeviceMidiBuffer : public daw::audio::DeviceMidiInput, public daw::audio::DeviceMidiOutput {
  public:
    int size() const override {
        return static_cast<int>(events.size());
    }
    const juce::MidiMessage& message(int index) const override {
        return events.at(index).message;
    }
    std::uint32_t sourceId(int index) const override {
        return events.at(index).sourceId;
    }
    bool isAllNotesOff() const override {
        return allNotesOff;
    }
    void addEvent(daw::audio::DeviceMidiEvent event) override {
        events.push_back(std::move(event));
    }
    void setAllNotesOff(bool value) override {
        allNotesOff = value;
    }

    std::vector<daw::audio::DeviceMidiEvent> events;
    bool allNotesOff = false;
};

}  // namespace magda::test
