#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include "audio/plugins/MagdaDevice.hpp"

namespace magda::test {

// Growable test storage, like the Tracktion view. clear() also consumes the
// buffer-level panic flag, so transforming devices must explicitly preserve it.
class DeviceMidiBuffer : public daw::audio::DeviceMidiBuffer {
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
    void setEvent(int index, daw::audio::DeviceMidiEvent event) override {
        events.at(index) = std::move(event);
    }
    void removeEvent(int index) override {
        events.erase(events.begin() + index);
    }
    void addEvent(daw::audio::DeviceMidiEvent event) override {
        events.push_back(std::move(event));
    }
    void clear() override {
        events.clear();
        allNotesOff = false;
    }
    void sortByTimestamp() override {
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return a.message.getTimeStamp() < b.message.getTimeStamp();
        });
    }
    bool isAllNotesOff() const override {
        return allNotesOff;
    }
    void setAllNotesOff(bool value) override {
        allNotesOff = value;
    }

    std::vector<daw::audio::DeviceMidiEvent> events;
    bool allNotesOff = false;
};

}  // namespace magda::test
