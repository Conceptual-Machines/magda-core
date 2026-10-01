#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <magda/sdk/device/ProcessContext.hpp>
#include <string>
#include <utility>

/**
 * @file DeviceJuceInterop.hpp
 * @brief The JUCE views a base-pack device takes of the SDK's JUCE-free block.
 *
 * Devices implement sdk::Device and keep their DSP and MIDI logic in JUCE types behind these
 * edges. Nothing here allocates for a short message, so the views are audio-thread safe.
 */

namespace magda::daw::audio {

/// The block's audio as a non-owning buffer over the host's channel pointers.
inline juce::AudioBuffer<float> juceAudio(const sdk::ProcessContext& context) {
    const auto& view = context.audio;
    if (view.numChannels() <= 0 || view.numFrames() <= 0)
        return {};
    return {view.channels(), view.numChannels(), view.numFrames()};
}

/// A device's state key, for a property named by a juce::Identifier.
inline std::string stateKey(const juce::Identifier& name) {
    return name.toString().toStdString();
}

/// A MIDI message and the source that produced it. The message's time is seconds from the block
/// start.
struct DeviceMidiEvent {
    juce::MidiMessage message;
    std::uint32_t sourceId = 0;
};

/// @p event as a juce message timed in seconds from the block start at @p sampleRate.
inline juce::MidiMessage toJuceMessage(const sdk::MidiEvent& event, double sampleRate) {
    const auto seconds = (event.sample + static_cast<double>(event.fraction)) / sampleRate;
    return juce::MidiMessage(event.data(), static_cast<int>(event.size()), seconds);
}

/**
 * @brief The MIDI that reached the device, as juce messages timed in seconds.
 *
 * Whether the stream continues past the device is the host's routing decision, never the
 * device's.
 */
class DeviceMidiInput {
  public:
    DeviceMidiInput(const sdk::MidiInput& input, double sampleRate)
        : input_(input), sampleRate_(sampleRate) {}

    int size() const {
        return input_.size();
    }

    juce::MidiMessage message(int index) const {
        return toJuceMessage(input_.event(index), sampleRate_);
    }

    std::uint32_t sourceId(int index) const {
        return input_.event(index).sourceId;
    }

    bool isAllNotesOff() const {
        return input_.isAllNotesOff();
    }

  private:
    const sdk::MidiInput& input_;
    double sampleRate_;
};

/// Where a device writes the MIDI it emits, from juce messages timed in seconds from the block
/// start.
class DeviceMidiOutput {
  public:
    DeviceMidiOutput(sdk::MidiOutput& output, double sampleRate)
        : output_(output), sampleRate_(sampleRate) {}

    /// False when the port's event count or byte budget is exhausted.
    bool addEvent(const DeviceMidiEvent& event) {
        const auto position = sdk::midiEventPosition(event.message.getTimeStamp(), sampleRate_);
        return output_.addEvent(sdk::MidiEvent::fromBytes(
            event.message.getRawData(), static_cast<std::uint32_t>(event.message.getRawDataSize()),
            position.sample, position.fraction, event.sourceId));
    }

    /// Panic beside the events, for a host whose MIDI container carries one.
    void setAllNotesOff(bool allNotesOff) {
        output_.setAllNotesOff(allNotesOff);
    }

  private:
    sdk::MidiOutput& output_;
    double sampleRate_;
};

}  // namespace magda::daw::audio
