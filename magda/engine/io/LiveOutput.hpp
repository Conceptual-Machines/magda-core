#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

/**
 * @file LiveOutput.hpp
 * @brief Where a hardware insert's send leaves the machine (#2279).
 */

namespace magda::engine {

/**
 * @brief The device output being rendered, for a live insert to add its send into.
 *
 * Every channel the callback has, not only those the plan's Output ops name: an
 * insert may send where no track goes. Narrowed per block by EngineSession::process
 * like LiveInputFeed. Audio thread only, within one callback.
 */
class LiveOutputFeed {
  public:
    /// Audio thread, once per process() call, before any block of it renders.
    void beginCallback(juce::AudioBuffer<float>& output) {
        output_ = &output;
    }

    /// Audio thread, per block, before the plan runs. @p startSample is the block's place
    /// in the process() call, which is what MIDI leaving with it is timed from.
    void beginSegment(int startSample, int numSamples) {
        segmentStart_ = startSample;
        segmentSamples_ = numSamples;
    }

    /// Audio thread, after the last block. Nothing reaches a block that has been played.
    void endCallback() {
        output_ = nullptr;
        segmentStart_ = 0;
        segmentSamples_ = 0;
    }

    /**
     * @brief Add @p numSamples of @p source to callback output @p channel, in this block.
     *
     * False, and nothing written, for a channel the callback does not have.
     */
    bool add(int channel, const float* source, int numSamples) {
        if (output_ == nullptr || channel < 0 || channel >= output_->getNumChannels() ||
            numSamples > segmentSamples_ || segmentStart_ + numSamples > output_->getNumSamples())
            return false;

        output_->addFrom(channel, segmentStart_, source, numSamples);
        return true;
    }

    int segmentStart() const {
        return segmentStart_;
    }

  private:
    juce::AudioBuffer<float>* output_ = nullptr;
    int segmentStart_ = 0;
    int segmentSamples_ = 0;
};

/**
 * @brief A hardware MIDI port, as a live insert sends to it (#2279).
 *
 * The host's: which port it is, and when a message timed against the callback
 * leaves the machine.
 */
class LiveMidiOutput {
  public:
    virtual ~LiveMidiOutput() = default;

    /**
     * @brief Send @p size bytes at @p sample from the start of the process() call.
     *
     * Audio thread, and without allocating or waiting. A message the port has no
     * room for is dropped and counted.
     */
    virtual void send(int sample, const std::uint8_t* data, int size) = 0;

    /// Off the audio thread: a note released by an insert going away. Sent once everything
    /// already queued has gone, so an off cannot overtake the on it ends.
    virtual void sendAfterQueued(const juce::MidiMessage& message) = 0;
};

}  // namespace magda::engine
