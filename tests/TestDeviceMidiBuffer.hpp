#pragma once

#include <utility>
#include <vector>

#include "audio/plugins/MagdaDevice.hpp"

namespace magda::test {

/**
 * @brief Growable test storage for either side of the SDK's MIDI contract.
 *
 * Fill `events` to feed a device, or hand an empty one over as the device's output. The events
 * are juce messages timed in seconds from the block start, which is how a test reads them; the
 * SDK's events are samples, so each side converts at @ref sampleRate.
 */
class DeviceMidiBuffer : public sdk::MidiInput, public sdk::MidiOutput {
  public:
    DeviceMidiBuffer() = default;
    explicit DeviceMidiBuffer(double rate) : sampleRate(rate) {}

    // Input
    int size() const override {
        return static_cast<int>(events.size());
    }
    const sdk::MidiEvent& event(int index) const override {
        if (decoded_.size() != events.size()) {
            decoded_.clear();
            for (const auto& entry : events) {
                const auto position =
                    daw::audio::midiEventPosition(entry.message.getTimeStamp(), sampleRate);
                decoded_.push_back(sdk::MidiEvent::fromBytes(
                    entry.message.getRawData(),
                    static_cast<std::uint32_t>(entry.message.getRawDataSize()), position.sample,
                    position.fraction, entry.sourceId));
            }
        }
        return decoded_.at(static_cast<std::size_t>(index));
    }
    bool isAllNotesOff() const override {
        return allNotesOff;
    }

    // Output
    bool addEvent(const sdk::MidiEvent& event) override {
        events.push_back({daw::audio::toJuceMessage(event, sampleRate), event.sourceId});
        return true;
    }
    void setAllNotesOff(bool value) override {
        allNotesOff = value;
    }

    /// What a test reads back: the message as the device wrote it, timed in seconds.
    const juce::MidiMessage& message(int index) const {
        return events.at(static_cast<std::size_t>(index)).message;
    }
    std::uint32_t sourceId(int index) const {
        return events.at(static_cast<std::size_t>(index)).sourceId;
    }

    std::vector<daw::audio::DeviceMidiEvent> events;
    bool allNotesOff = false;
    double sampleRate = 48000.0;

  private:
    mutable std::vector<sdk::MidiEvent> decoded_;
};

}  // namespace magda::test
