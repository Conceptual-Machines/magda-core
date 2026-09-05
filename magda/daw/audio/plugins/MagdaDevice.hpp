#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <string_view>

#include "core/ParameterInfo.hpp"

namespace magda::daw::audio {

struct DeviceProperties {
    juce::String pluginId;
    juce::String name;
    juce::String shortName;
    bool takesMidiInput = false;
    /// The device emits MIDI of its own, written to DeviceProcessContext::midiOut.
    /// Its input never passes through it: thru is the host's merge (#2347).
    bool producesMidi = false;
    bool takesAudioInput = true;
    bool isSynth = false;
    bool producesAudioWithoutInput = false;
    bool canSidechain = false;
    double latencySeconds = 0.0;
    double tailLengthSeconds = 0.0;
    /// Output channels the device always produces, whatever it is handed. Zero
    /// means it follows its input, which is what most effects do; a device sets
    /// this when its DSP has a fixed output width (a mono-in/stereo-out
    /// widener, a stereo-only dynamics stage).
    int outputChannelCount = 0;
    /// Input channels the device reads, sidechain key included. Zero means the
    /// host decides. What the model wires from: a device asking for more inputs
    /// than it outputs is asking for a key.
    int inputChannelCount = 0;
};

struct DevicePrepareContext {
    double sampleRate = 44100.0;
    int maximumBlockSize = 0;
};

struct DeviceMidiEvent {
    juce::MidiMessage message;
    std::uint32_t sourceId = 0;
};

/**
 * Read-only musical time supplied for the duration of a process call.
 */
class DeviceTempoMap {
  public:
    virtual ~DeviceTempoMap() = default;

    virtual double beatsAtSeconds(double seconds) const = 0;
    virtual double bpmAtSeconds(double seconds) const = 0;
};

/**
 * Base for device-owned, engine-neutral telemetry surfaces.
 *
 * Concrete devices may expose typed subclasses. The returned telemetry object
 * is owned by the device and has the same lifetime.
 */
class DeviceTelemetry {
  public:
    virtual ~DeviceTelemetry() = default;

    virtual std::string_view telemetryKey() const = 0;
};

/**
 * The MIDI that reached the device this block. Timestamps are seconds from
 * the block start.
 *
 * Read-only: whether this stream continues past the device is the host's
 * routing decision (DeviceInfo::midiInThru), never the device's (#2347).
 */
class DeviceMidiInput {
  public:
    virtual ~DeviceMidiInput() = default;

    virtual int size() const = 0;
    virtual const juce::MidiMessage& message(int index) const = 0;
    virtual std::uint32_t sourceId(int index) const = 0;
    /// The host signalled panic without a CC event (a playhead jump, a stop).
    virtual bool isAllNotesOff() const = 0;
};

/**
 * Where a device writes the MIDI it emits. Empty on entry; on exit it is the
 * device's whole MIDI output. Timestamps are seconds from the block start.
 */
class DeviceMidiOutput {
  public:
    virtual ~DeviceMidiOutput() = default;

    virtual void addEvent(DeviceMidiEvent event) = 0;
    /// Panic beside the events, for a host whose MIDI container carries one.
    virtual void setAllNotesOff(bool allNotesOff) = 0;
};

struct DeviceProcessContext {
    juce::AudioBuffer<float>* audio = nullptr;
    /**
     * First channel of @ref audio carrying the device's sidechain key, or -1
     * when the host routed nothing to it.
     *
     * The key arrives as further channels of the same buffer, after the ones
     * the device reads and writes as its own signal, which is how MAGDA's
     * compiled dynamics DSPs are written and what both hosts can supply
     * without a copy. A device with no sidechain never sees anything above its
     * own width.
     */
    int sidechainInputChannel = -1;
    /// Both null when the host routed no MIDI to or from the device, otherwise
    /// both set; a device that declares no MIDI output still gets a sink, which
    /// the host discards.
    const DeviceMidiInput* midiIn = nullptr;
    DeviceMidiOutput* midiOut = nullptr;
    const DeviceTempoMap* tempoMap = nullptr;
    int startSample = 0;
    int numSamples = 0;
    double midiTimeOffsetSeconds = 0.0;
    double timelineStartSeconds = 0.0;
    double timelineEndSeconds = 0.0;
    bool isPlaying = false;
    bool isScrubbing = false;
    bool isRendering = false;
    /**
     * Sources the host counts as live input, against DeviceMidiInput::sourceId.
     *
     * A device that holds notes needs this to tell a player's keys from clip
     * playback: a clip's note-off never arrives once the transport stops, and
     * a seek re-asserts what sounds at the destination without releasing what
     * sounded only at the origin. Empty when the host does not say, which a
     * device must read as "no source is known live", never as "all of them".
     */
    const std::uint32_t* liveSourceIds = nullptr;
    int numLiveSourceIds = 0;
};

/**
 * Engine-neutral device/DSP contract.
 *
 * Concrete MAGDA devices implement this interface. Audio-engine integrations
 * adapt it to their plugin or render-plan lifecycle; host-specific plugin
 * classes must not leak into device-pack code.
 */
class MagdaDevice {
  public:
    virtual ~MagdaDevice() = default;

    // Properties are constant for the lifetime of the device.
    virtual DeviceProperties properties() const = 0;

    virtual void prepare(const DevicePrepareContext&) {}
    virtual void release() {}
    virtual void reset() {}
    virtual void process(DeviceProcessContext&) = 0;

    /**
     * Parameter values are normalized to [0, 1]. ParameterInfo describes the
     * corresponding display-domain range, scale, unit, and choices.
     */
    virtual int parameterCount() const {
        return 0;
    }
    virtual ParameterInfo parameterInfo(int) const {
        return {};
    }
    virtual float parameterValue(int) const {
        return 0.0f;
    }
    virtual void setParameterValue(int, float) {}

    virtual void flushState(juce::ValueTree&) {}
    virtual void restoreState(const juce::ValueTree&) {}

    virtual DeviceTelemetry* telemetry(std::string_view) {
        return nullptr;
    }
    virtual const DeviceTelemetry* telemetry(std::string_view) const {
        return nullptr;
    }
};

}  // namespace magda::daw::audio
