#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "exec/RenderContext.hpp"

/**
 * @file EngineDevice.hpp
 * @brief The runtime objects a plan's leaf ops stand for.
 *
 * The plan is pure topology: a Device op names a DeviceId, it does not own a
 * plugin, and a ClipAudio op names a track, not a file reader. The host binds
 * the objects and outlives the plans that reference them, which is what lets
 * the differ carry a live instrument across a structural change instead of
 * rebuilding it.
 */

namespace magda::engine {

/**
 * @brief Events one source or device may emit into one port in one block.
 *
 * The executor reserves storage for every MIDI port before the first block, so
 * nothing on the audio thread has to grow a buffer. A port fed by a merge is
 * sized from the sum of what feeds it; the chain has to end somewhere, and this
 * is where: a source or device that writes more than this forces the very
 * allocation the reservation exists to avoid. Debug builds assert on it.
 *
 * Deliberately generous. A block of dense controller traffic is a few dozen
 * events, and a chord is a handful.
 */
constexpr int kMaxMidiEventsPerPort = 256;

/** Audio and MIDI handed to a device for one block. */
struct DeviceBlock {
    /// The device's audio input on entry, its output on exit. Devices process
    /// in place: every op renders into its own buffer, so overwriting the input
    /// cannot disturb another consumer of the same signal.
    juce::dsp::AudioBlock<float> audio;

    /// MIDI reaching the device. Empty when the plan left the slot unconnected.
    const juce::MidiBuffer* midiIn = nullptr;

    /// Where a MIDI-producing device writes, cleared before the call. Null when
    /// the plan gave the device no MIDI output port. At most
    /// kMaxMidiEventsPerPort events; beyond that the buffer has to allocate.
    juce::MidiBuffer* midiOut = nullptr;

    /// Sidechain audio. A zero-channel block when the slot is unconnected, so
    /// devices check getNumChannels() rather than assuming a source.
    juce::dsp::AudioBlock<const float> sidechain;

    BlockInfo block;
};

/**
 * @brief One device instance behind a Device op.
 *
 * Bound by DeviceId, owned by the host. process() runs on the audio thread:
 * no allocation, no locks, no file or GUI work.
 */
class EngineDevice {
  public:
    virtual ~EngineDevice() = default;

    /// Called off the audio thread before the first block.
    virtual void prepare(const RenderContext&) {}

    /// Drop any tail state. Called off the audio thread.
    virtual void reset() {}

    /// Samples the device delays its output by. Read but not yet compensated:
    /// latency compensation is its own slice, and the executor reports any
    /// non-zero value from prepare() rather than pretending it is aligned.
    virtual int latencySamples() const {
        return 0;
    }

    virtual void process(DeviceBlock&) = 0;
};

/** An audio source behind a ClipAudio or AudioInput op. */
class EngineAudioSource {
  public:
    virtual ~EngineAudioSource() = default;

    virtual void prepare(const RenderContext&) {}

    /// Fill @p out completely; it arrives uncleared.
    virtual void render(const BlockInfo&, juce::dsp::AudioBlock<float> out) = 0;
};

/** A MIDI source behind a ClipMidi or MidiInput op. */
class EngineMidiSource {
  public:
    virtual ~EngineMidiSource() = default;

    virtual void prepare(const RenderContext&) {}

    /// Add this block's events to @p out; it arrives cleared. At most
    /// kMaxMidiEventsPerPort of them.
    virtual void render(const BlockInfo&, juce::MidiBuffer& out) = 0;
};

}  // namespace magda::engine
