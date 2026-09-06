#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "exec/EngineDevice.hpp"
#include "exec/RenderContext.hpp"

/**
 * @file LiveInput.hpp
 * @brief What the audio device just handed the engine, and the sources reading it (#2459).
 *
 * An AudioInput or MidiInput op is compiled for a track that is armed or
 * monitoring, and the plan says nothing about where the signal comes from
 * (PlanCompiler.cpp). These are the objects behind those ops: one live
 * source per track, reading the callback's own input rather than a buffer
 * filled at some other time.
 *
 * Which hardware channels a track's `audioInputDevice` names, and which
 * device its `midiInputDevice` names, is the host's to resolve: the engine
 * knows channel indices and an opaque source id, never a device name.
 */

namespace magda::engine {

/**
 * @brief Which MIDI input a stream came from.
 *
 * Opaque to the engine. The host assigns one per enabled device and resolves
 * a track's model string to it; kAnyLiveMidiSource is the "all" that string
 * also accepts, and merges every stream in the callback.
 */
using LiveMidiSourceId = int;

constexpr LiveMidiSourceId kAnyLiveMidiSource = -1;

/** @brief One MIDI input's events for a callback, stamped from its first sample. */
struct LiveMidiStream {
    LiveMidiSourceId source = kAnyLiveMidiSource;
    const juce::MidiBuffer* events = nullptr;
};

/**
 * @brief The device's input for one callback.
 *
 * Handed to EngineSession::process alongside the output it fills. Views, not
 * copies: everything here belongs to the caller and lives for the call.
 */
struct LiveInputBlock {
    /// The hardware input channels, in the order the device reports them.
    juce::dsp::AudioBlock<const float> audio;

    /// One entry per enabled MIDI input. Empty for a host with none, which is
    /// every offline render.
    std::span<const LiveMidiStream> midi;
};

/**
 * @brief The callback's input, narrowed to the block a source is rendering.
 *
 * A callback is one or more blocks: the transport cuts it at every loop wrap
 * (EngineSession::process), and each piece renders separately. The input is
 * not timeline material and knows nothing about that, so the feed holds the
 * callback's input once and hands out the window belonging to the piece in
 * flight.
 *
 * Held as a copy rather than as the caller's pointers, because a host may
 * hand the same memory for input and output -- an AudioProcessor is given one
 * buffer for both -- and the render clears and fills the output before an
 * input op reads anything. A borrowed view of that memory would be zeros by
 * the time a source looked at it.
 *
 * The room for the copy is taken by @ref prepare, so the copy itself cannot
 * allocate. Written and read on the audio thread only, within one callback.
 */
class LiveInputFeed {
  public:
    /**
     * @brief Room for what a callback may deliver. Off the audio thread.
     *
     * Called when the audio device opens or changes, like every other prepare
     * in the engine, and never while a callback can run: it allocates. It only
     * ever grows, so a session preparing for its plan cannot shrink what the
     * host sized for its interface.
     *
     * A feed with no room reads silence and counts the callback
     * (@ref unfitCallbacks), which is what a host that never prepared gets.
     */
    void prepare(int maxChannels, int maxBlockSize);

    /// Audio thread, once per callback, before any block of it renders and
    /// before anything writes to the output.
    void beginCallback(const LiveInputBlock& input, int numSamples);

    /// Audio thread, per block of that callback, before the plan runs.
    void beginSegment(int startSample, int numSamples);

    /// Audio thread, after the last block. A source reaching the feed outside
    /// a callback reads nothing rather than the previous callback's samples.
    void endCallback();

    /// The current block's input audio, empty when the host supplied none.
    juce::dsp::AudioBlock<const float> audio() const {
        return audio_;
    }

    /**
     * @brief Append @p source's events for this block to @p out, offset to it.
     *
     * kAnyLiveMidiSource merges every stream. Events land at the sample
     * offsets the host stamped, which is what makes a live note recordable at
     * the position it was played rather than at a block boundary.
     *
     * Stops at @p maxBytes of encoded MIDI and counts what would not fit.
     */
    int appendEvents(LiveMidiSourceId source, juce::MidiBuffer& out, int maxBytes) const;

    /// Callbacks whose input did not fit the room prepare() took, in channels
    /// or in samples. Read from any thread; what did not fit reads silence.
    std::uint32_t unfitCallbacks() const {
        return unfit_.load(std::memory_order_relaxed);
    }

    int preparedChannels() const {
        return scratch_.getNumChannels();
    }

    int preparedBlockSize() const {
        return scratch_.getNumSamples();
    }

  private:
    juce::AudioBuffer<float> scratch_;
    juce::dsp::AudioBlock<const float> audio_;
    std::span<const LiveMidiStream> midi_;
    int callbackChannels_ = 0;
    int callbackSamples_ = 0;
    int segmentStart_ = 0;
    int segmentSamples_ = 0;
    std::atomic<std::uint32_t> unfit_{0};
};

/**
 * @brief A track's live audio input: the hardware channels it was pointed at.
 *
 * Channels are resolved once, off the audio thread, and are indices into what
 * the device delivers. A destination wider than the source repeats the
 * source's last channel across the rest, which is what the current engine
 * does (`WaveInputDeviceNode::process` copies its last read channel over the
 * remaining destination channels), so a mono input is heard in both ears
 * rather than only the left.
 */
class LiveAudioInput final : public EngineAudioSource {
  public:
    /**
     * @brief Reads @p channels of @p feed.
     *
     * @p latencySamples is what the device reports for its input: see
     * @ref latencySamples for what it is and is not for.
     */
    LiveAudioInput(const LiveInputFeed& feed, std::span<const int> channels,
                   int latencySamples = 0);

    void render(const BlockInfo& /*block*/, juce::dsp::AudioBlock<float> out) override;

    /**
     * @brief The input's own latency, in samples.
     *
     * Not a plan latency and deliberately not declared to the compensation
     * pass: what arrives has already happened, so delaying the rest of the
     * graph to match it would push playback and the click behind the live
     * signal rather than align anything. It is the number a recorded take is
     * placed by (#2461), which is the only thing that can act on it.
     */
    int latencySamples() const {
        return latencySamples_;
    }

    /// Blocks that found fewer channels than the map named. Read from any
    /// thread; a wrong channel count is silence, which nothing else reports.
    std::uint32_t missingChannelBlocks() const {
        return missingChannels_.load(std::memory_order_relaxed);
    }

  private:
    const LiveInputFeed& feed_;
    std::vector<int> channels_;
    int latencySamples_ = 0;
    std::atomic<std::uint32_t> missingChannels_{0};
};

/**
 * @brief A track's live MIDI input, from one device or from all of them.
 *
 * The events are the host's, already stamped against the callback. The
 * current engine collects messages between callbacks and places them
 * relative to the first one's timestamp, which its own comment calls "near
 * enough for live stuff" (`MidiInputDeviceNode::processSection`); a stamp
 * taken against the block the event arrived in is what slice #2462 needs to
 * record a note where it was played.
 */
class LiveMidiInput final : public EngineMidiSource {
  public:
    explicit LiveMidiInput(const LiveInputFeed& feed, LiveMidiSourceId source = kAnyLiveMidiSource,
                           int latencySamples = 0);

    void render(const BlockInfo& /*block*/, juce::MidiBuffer& out) override;

    /// As LiveAudioInput::latencySamples, for the MIDI port.
    int latencySamples() const {
        return latencySamples_;
    }

    /// Events dropped for want of room in the port's byte budget
    /// (kMaxMidiBytesPerPort). Read from any thread.
    std::uint32_t droppedEvents() const {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    const LiveInputFeed& feed_;
    LiveMidiSourceId source_ = kAnyLiveMidiSource;
    int latencySamples_ = 0;
    std::atomic<std::uint32_t> dropped_{0};
};

}  // namespace magda::engine
