#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <span>

#include "exec/RenderContext.hpp"
#include "param/ParamBlock.hpp"

/**
 * @file EngineDevice.hpp
 * @brief The runtime objects a plan's leaf ops stand for.
 *
 * The plan is pure topology: a Device op names a section-aware device identity,
 * it does not own a plugin, and a ClipAudio op names a track, not a file reader.
 * The host binds the objects and outlives the plans that reference them, which
 * is what lets the differ carry a live instrument across a structural change
 * instead of rebuilding it.
 */

namespace magda::engine {

/**
 * @brief Encoded MIDI one source or device may write to one port over any
 *        RenderContext::maxBlockSize samples of the timeline.
 *
 * The executor reserves storage for every MIDI port before the first block,
 * so nothing on the audio thread grows a buffer; a producer writing past this
 * forces the allocation the reservation exists to avoid (debug builds assert
 * on it at every write site). A port fed by a merge is sized from the sum of
 * what feeds it.
 *
 * Sized per span of samples, not per callback: blocks may be shorter than
 * the plan was prepared for, so a host delivering one sample at a time
 * against a 512-sample plan fits 512 callbacks in the same stretch of
 * timeline, and a per-callback figure would be short by that factor. Every
 * MIDI producer is positioned on the timeline for this reason, not per call.
 *
 * The debug asserts only see one callback, so the actual enforcement is
 * downstream, at the one place a violation costs something: a delay line
 * holding more than it reserved room for reports it
 * (MidiDelayLine::hasOverflowed), in release as well as debug.
 *
 * Budgeted in bytes rather than events, since a MIDI message has no fixed
 * size (one SysEx dump can outweigh a thousand notes). An event costs six
 * bytes plus its own length, so a note or controller change is nine, and
 * this budget holds around 450 of them.
 */
constexpr int kMaxMidiBytesPerPort = 4096;

/// What one short message costs against kMaxMidiBytesPerPort.
constexpr int kMidiShortMessageBytes = 9;

/** Audio and MIDI handed to a device for one block. */
struct DeviceBlock {
    /// The device's audio input on entry, its output on exit. Devices process
    /// in place: every op renders into its own buffer, so overwriting the input
    /// cannot disturb another consumer of the same signal.
    juce::dsp::AudioBlock<float> audio;

    /// MIDI reaching the device. Empty when the plan left the slot unconnected.
    const juce::MidiBuffer* midiIn = nullptr;

    /**
     * @brief Panic reaching the device: release what you are holding (#2418).
     *
     * A discontinuity the host signals without a CC event -- a playhead jump,
     * a track that just went inaudible -- after which it re-asserts what
     * should be sounding without a note-off for what should not. The fork
     * carries it on the buffer (MidiMessageArray::isAllNotesOff); a
     * juce::MidiBuffer has no room for it, so it travels beside the port.
     */
    bool midiInAllNotesOff = false;

    /**
     * @brief Where a MIDI-producing device writes, cleared before the call.
     *
     * Null when the plan gave the device no MIDI output port. At most what
     * setMidiOutputBoundBytes said; past it the buffer allocates.
     *
     * What the device produced, not what it was handed: MIDI thru is the
     * plan's own merge behind the device (DeviceInfo::midiInThru), so a
     * device that also echoed its input would double every note (#2345).
     */
    juce::MidiBuffer* midiOut = nullptr;

    /**
     * @brief The panic the device leaves on @ref midiOut, read back after
     *        process() and carried to whatever the port feeds.
     *
     * False on entry, like the buffer: a device producing MIDI produces the
     * flag with it, and one meaning to pass a panic on says so. Ignored for a
     * device the plan gave no MIDI output.
     */
    bool midiOutAllNotesOff = false;

    /// Sidechain audio. A zero-channel block when the slot is unconnected, so
    /// devices check getNumChannels() rather than assuming a source.
    juce::dsp::AudioBlock<const float> sidechain;

    /**
     * @brief A multi-out instrument's further output pairs.
     *
     * `extraOutputs[k]` is pair k + 1: pair 0 is `audio` above. Empty for
     * every device that is not multi-out (almost all of them).
     *
     * One block per pair the device declares, whether or not a MultiOut
     * track was opened for it -- a device writes its own layout and never
     * asks what's being listened to, matching the current engine (the
     * instrument writes every rack pin; the RackInstance decides who reads
     * it). Each block arrives cleared, so a device writing only the pairs it
     * has material for leaves the rest silent rather than stale.
     */
    std::span<juce::dsp::AudioBlock<float>> extraOutputs;

    /**
     * @brief The device's parameters, resolved for this block (#2116).
     *
     * Indexed the way the device declared them. Each entry is already the
     * stored value, its automation curve and its linked modifiers resolved
     * into one clamped, quantised value stream in the parameter's own
     * units -- a device has no other way to read a parameter, which keeps
     * the precedence rules in one place (param/ParamResolve.hpp).
     *
     * Resolved before this call and before the MIDI above, so an event at
     * sample zero sees this block's automated value, not the previous one.
     * Empty for a device with no parameters, or before the table is first
     * published against a plan.
     */
    DeviceParams params;

    BlockInfo block;
};

/**
 * @brief One device instance behind a Device op.
 *
 * Bound by DeviceKey, owned by the host. process() runs on the audio thread: no
 * allocation, no locks, no file or GUI work.
 */
class EngineDevice {
  public:
    virtual ~EngineDevice() = default;

    /// Called off the audio thread before the first block.
    virtual void prepare(const RenderContext&) {}

    /// Drop any tail state. Called off the audio thread.
    virtual void reset() {}

    /**
     * @brief The most encoded MIDI that can reach this device in one block.
     *
     * Called off the audio thread when a plan is prepared, by the executor
     * that knows the answer. A device that buffers its input sizes that
     * buffer from this and nothing else.
     *
     * Not kMaxMidiBytesPerPort: that's one producer's budget, while a
     * device's input port is often a merge, and the executor sums the bound
     * through the MIDI graph so a device assuming the per-producer cap
     * wouldn't drop input on a track fed by more than one source. Zero for a
     * device the plan gave no MIDI input. Ignored by default, since most
     * devices read the block's buffer where it lies without needing to know
     * how big it can get.
     */
    virtual void setMidiInputBoundBytes(int) {}

    /**
     * @brief The most encoded MIDI this device may write in one block.
     *
     * Called off the audio thread beside setMidiInputBoundBytes, for the
     * same reason: the figure belongs to the plan, not the device.
     *
     * A device is a producer only -- thru is the plan's merge, not the
     * device's (#2345) -- so this is one producer's worth, sized for what
     * the device itself makes (a pattern, an arpeggio, a post-prepare
     * all-notes-off). Passed in rather than assumed because the port owns
     * the figure and a device reading a constant instead would go silently
     * wrong the day an op behind it reserves differently. Zero for a device
     * the plan gave no MIDI output; deliberately not the (often larger)
     * input bound, since a device may read a merged stream and emit only
     * its own. Ignored by default.
     */
    virtual void setMidiOutputBoundBytes(int) {}

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
    /// kMaxMidiBytesPerPort of encoded MIDI, SysEx included.
    virtual void render(const BlockInfo&, juce::MidiBuffer& out) = 0;

    /// Whether the block just rendered was a discontinuity of the source's
    /// own -- a slot the session launched where the transport never moved
    /// (#2418). Raises the panic flag on the port it feeds, beside the one
    /// every device takes from BlockInfo::continuous. Read straight after
    /// render(); false for a source that only follows the timeline.
    virtual bool raisedAllNotesOff() const {
        return false;
    }
};

/**
 * @brief One hardware insert: what leaves the machine, and what comes back (#2245).
 *
 * An insert is a send op and a return op in the plan, both resolving to one
 * of these -- one object rather than a source and a sink, since a round trip
 * is one thing and the latency below is a property of the pair.
 *
 * Offline rendering (a bounce played back from a capture rather than run at
 * real time, since the outside world can't be sped up) is not modeled here:
 * an implementation returning captured samples and one returning what an
 * interface just handed it satisfy the same two calls, and which one a
 * render binds is what decides. Same shape as the location-transparent
 * device op, for the same reason (#1893).
 *
 * There is deliberately no notion of an implementation that exists but is
 * unfit to render (a capture at the wrong sample rate, one with a gap in
 * it): the executor just binds a pointer and calls it, so a "check me
 * first" flag would be a rule some caller could forget with nothing
 * downstream able to catch it. A capture that can't be replayed must fail
 * to construct rather than fail to be valid -- a decision for whatever owns
 * captures (#2279).
 */
class EngineInsert {
  public:
    virtual ~EngineInsert() = default;

    virtual void prepare(const RenderContext&) {}

    /// Drop anything in flight. Called off the audio thread.
    virtual void reset() {}

    /**
     * @brief The round trip, in samples, as this insert measures it.
     *
     * Asked when a plan is prepared, beside every device's, and compensated
     * by the same pass -- an insert's round trip is a latency on an edge,
     * so a chain with an insert lines up with one without.
     *
     * The user's manual correction lives here rather than applied
     * elsewhere, since it's part of the same number: the interface reports
     * its own buffering and knows nothing of the converter and cable past
     * it, so what the insert measures and what a person measured with a
     * loopback end up as one figure by the time anything compensates.
     */
    virtual int latencySamples() const {
        return 0;
    }

    /**
     * @brief What is leaving the machine this block.
     *
     * Independent of @ref receive within one block: what comes back now
     * left several blocks ago, so there's no ordering between the two and
     * the plan declares none -- an implementation must not carry state from
     * one to the other inside a block.
     *
     * Either may be empty, which is an insert with no send of that kind (an
     * external effect sends audio and no MIDI, an external instrument the
     * reverse). Called on the audio thread.
     */
    virtual void send(const BlockInfo&, juce::dsp::AudioBlock<const float> audio,
                      const juce::MidiBuffer& midi) = 0;

    /**
     * @brief What came back.
     *
     * @p audio arrives uncleared and must be filled completely, like an audio
     * source's; @p midi arrives cleared. An insert with no return of that kind
     * is not asked for it.
     */
    virtual void receive(const BlockInfo&, juce::dsp::AudioBlock<float> audio,
                         juce::MidiBuffer& midi) = 0;
};

}  // namespace magda::engine
