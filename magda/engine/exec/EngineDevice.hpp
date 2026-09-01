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
 * The executor reserves storage for every MIDI port before the first block, so
 * nothing on the audio thread has to grow a buffer. A port fed by a merge is
 * sized from the sum of what feeds it; the chain has to end somewhere, and this
 * is where: a producer that writes past this forces the very allocation the
 * reservation exists to avoid. Debug builds assert on it at every write site.
 *
 * The budget is per span of samples rather than per callback, and the
 * difference is not pedantry. Blocks may be shorter than the one the plan was
 * prepared for, so a callback is not a fixed amount of time: a host delivering
 * one sample at a time against a plan prepared for five hundred and twelve
 * would fit five hundred and twelve callbacks inside the same stretch of
 * timeline, and any storage sized from a per-callback figure would be short by
 * that factor. Nothing noticed while every reservation covered one block; a
 * delay line holds what is in flight across many, and it is what the amount of
 * time a callback stands for has to be pinned down for.
 *
 * Every producer the engine has already works this way, because MIDI is
 * positioned on the timeline rather than per call: a clip source renders the
 * events its block's time range contains, live input delivers what arrived
 * while that time passed, and a generator places notes at musical positions.
 * Spelling it out is what lets storage be sized from it.
 *
 * The debug asserts at the write sites see one callback and so can only check
 * the looser reading of this, which is all a producer can be caught at where it
 * writes. What actually enforces the budget is downstream, at the one place a
 * violation costs something: a delay line holding more than it reserved room
 * for reports it (MidiDelayLine::hasOverflowed), in release as well as debug.
 *
 * The budget is bytes rather than events because a MIDI message is not a fixed
 * size: one SysEx dump from a controller can outweigh a thousand notes, and a
 * cap on the number of events would let it through. An event costs six bytes
 * plus its own length, so a note or a controller change is nine, and this
 * budget holds around four hundred and fifty of them.
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

    /// Where a MIDI-producing device writes, cleared before the call. Null when
    /// the plan gave the device no MIDI output port. At most
    /// kMaxMidiBytesPerPort of encoded MIDI; past that the buffer allocates.
    juce::MidiBuffer* midiOut = nullptr;

    /// Sidechain audio. A zero-channel block when the slot is unconnected, so
    /// devices check getNumChannels() rather than assuming a source.
    juce::dsp::AudioBlock<const float> sidechain;

    /**
     * @brief A multi-out instrument's further output pairs.
     *
     * `extraOutputs[k]` is pair k + 1: pair 0 is `audio` above, which is what
     * the device's own chain carries on from. Empty for every device that is
     * not multi-out, which is almost all of them.
     *
     * One block per pair the device declares, whether or not a MultiOut track
     * was opened for it, so a device writes its outputs where its own layout
     * says they go and never has to ask what is being listened to. That is what
     * the current engine does too: the instrument writes every pin of the rack
     * around it, and it is the RackInstance for a pair that decides whether
     * anyone reads those pins. A pair no track opened is rendered and dropped.
     *
     * Each block arrives cleared, so a device that writes only the pairs it has
     * material for leaves the rest silent rather than stale.
     */
    std::span<juce::dsp::AudioBlock<float>> extraOutputs;

    /**
     * @brief The device's parameters, resolved for this block (#2116).
     *
     * Indexed the way the device declared them, and already everything the
     * device could want to know: the stored value, the automation curve over
     * it and the modifiers linked to it have been resolved into one value
     * stream per parameter, clamped, quantised, and in the parameter's own
     * units. A device reads these and has no other way to find out what a
     * parameter is, which is what keeps the precedence rules in one place
     * (param/ParamResolve.hpp) rather than in every device.
     *
     * Resolved before this call and before the MIDI above is looked at, so an
     * event at sample zero sees the value automation put there rather than the
     * one from the block before.
     *
     * Empty for a device with no parameters, and for every device until the
     * table is published against a plan.
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
     * Called off the audio thread when a plan is prepared, before any block, by
     * the executor that knows the answer. A device that buffers its input sizes
     * that buffer from this and nothing else.
     *
     * Not kMaxMidiBytesPerPort. That is one producer's budget, and a device's
     * input port is often a merge: the executor sums the bound through the MIDI
     * graph precisely because fan-in outgrows any fixed figure, and a device
     * that assumed the per-producer cap would drop everything past it on a
     * track with more sources than one. Zero for a device the plan gave no MIDI
     * input.
     *
     * Ignored by default, because most devices read the block's buffer where it
     * lies and never need to know how big it can get.
     */
    virtual void setMidiInputBoundBytes(int) {}

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
};

/**
 * @brief One hardware insert: what leaves the machine, and what comes back
 *        (#2245).
 *
 * The outside world, as the engine is allowed to see it. An insert is a send op
 * and a return op in the plan, and both of them resolve to one of these: the
 * send hands over what is leaving, the return is asked for what arrived, and
 * how those reach an interface is the host's business exactly as a live input's
 * is.
 *
 * One object rather than a source and a sink, because a round trip is one
 * thing. The latency below is a property of the pair -- what left, coming back
 * -- and an implementation that had to correlate a separately bound sink with a
 * separately bound source would be rebuilding the pairing the plan already did.
 *
 * ## The offline case
 *
 * A bounce cannot run the outside world faster than real time, so it either
 * runs at real time or plays back a capture, and the incumbent does the latter.
 * Nothing about that is here, and that is the seam rather than a gap: an
 * implementation that returns captured samples and one that returns what an
 * interface just handed it satisfy the same two calls, and which one a render
 * binds is what decides. Same shape as the location-transparent device op
 * (#1893), for the same reason.
 *
 * What this interface deliberately does not have is a notion of an
 * implementation that is present but unfit to render -- a capture taken at
 * another sample rate, one with a gap in it. Nothing here could enforce such a
 * state: the executor binds a pointer and calls it, so a "check me first" flag
 * would be a rule some caller has to remember, and the render that forgot would
 * be wrong with nothing downstream able to see it. A capture that cannot be
 * replayed must therefore fail to be constructed rather than fail to be valid,
 * which is a decision for whatever owns captures (#2279) and is why none of it
 * is here.
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
     * Asked when a plan is prepared, beside every device's, and compensated by
     * the same pass: an insert reporting a round trip is a latency on an edge,
     * and the graph aligns it against everything else arriving where it lands.
     * That is what makes a chain with an insert in it line up with a chain
     * without one, and it is why this is not an insert-shaped special case.
     *
     * The user's manual correction is included here rather than applied
     * somewhere above, because it is part of the same number: the interface
     * reports its own buffering and knows nothing about the converter and the
     * cable past it, so what an insert measures and what a person measured with
     * a loopback are one figure by the time anything compensates for it.
     */
    virtual int latencySamples() const {
        return 0;
    }

    /**
     * @brief What is leaving the machine this block.
     *
     * Independent of @ref receive within one block, and deliberately: what comes
     * back now is what left several blocks ago, so there is no ordering between
     * the two and the plan declares none. The scheduler is free to run them on
     * different threads, which means an implementation must not carry state
     * from one to the other inside a block.
     *
     *
     * Either may be empty, which is an insert with no send of that kind: an
     * external effect sends audio and no MIDI, an external instrument the
     * reverse. Called on the audio thread.
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
