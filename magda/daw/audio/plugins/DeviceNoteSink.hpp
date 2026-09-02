#pragma once

#include <utility>

#include "plugins/MagdaDevice.hpp"
#include "sequencer/NoteSink.hpp"

namespace magda::daw::audio {

/**
 * @brief Carries the sequencing core's note events into a device's MIDI buffer.
 *
 * The core names no MIDI container (#2313), so this is the one place that turns
 * its plain `{time, note, velocity, on/off}` records into the messages a host
 * hands downstream. Timestamps stay in seconds from the block's start, which is
 * the domain DeviceMidiBuffer takes.
 */
class DeviceNoteSink : public sequencer::NoteSink {
  public:
    explicit DeviceNoteSink(DeviceMidiBuffer& midi) : midi_(midi) {}

    void addNoteEvent(const sequencer::NoteEvent& event) override {
        auto message = event.isNoteOn
                           ? juce::MidiMessage::noteOn(1, event.noteNumber,
                                                       static_cast<juce::uint8>(event.velocity))
                           : juce::MidiMessage::noteOff(1, event.noteNumber);
        message.setTimeStamp(event.timeInBlock);
        midi_.addEvent({std::move(message), 0});
    }

  private:
    DeviceMidiBuffer& midi_;
};

}  // namespace magda::daw::audio
