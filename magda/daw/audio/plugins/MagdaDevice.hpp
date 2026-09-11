#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <string_view>

#include "core/ParameterInfo.hpp"
#include "core/SidechainPort.hpp"

namespace magda::daw::audio {

struct DeviceProperties {
    juce::String pluginId;
    juce::String name;
    juce::String shortName;
    bool takesMidiInput = false;
    /// The device emits MIDI of its own, written to DeviceProcessContext::midiOut.
    /// Its input never passes through it: thru is the host's merge (#2347).
    bool producesMidi = false;
    /// The device copies part of its input onto its output: what it consumes
    /// is its material, and the rest belongs to whatever plays its notes (the
    /// arpeggiator's non-note traffic, #2417). Not thru, which is the host's
    /// whole-stream merge; a host sizes this device's MIDI output for its
    /// input as well as its own production.
    bool forwardsMidiInput = false;
    bool takesAudioInput = true;
    bool isSynth = false;
    bool producesAudioWithoutInput = false;
    /// The sidechain slot the device asks for, if it wants one (#2329).
    /// Declared here rather than read off the channel counts below: a device
    /// with more inputs than outputs is not by itself asking for a key.
    magda::SidechainPort sidechain;
    double latencySeconds = 0.0;
    double tailLengthSeconds = 0.0;
    /// Output channels the device always produces, whatever it is handed. Zero
    /// means it follows its input, which is what most effects do; a device sets
    /// this when its DSP has a fixed output width (a mono-in/stereo-out
    /// widener, a stereo-only dynamics stage).
    int outputChannelCount = 0;
    /// Input channels the device reads for its own signal, the sidechain key
    /// not among them: the key is a port (@ref sidechain), not the tail of this
    /// count. Zero means the host decides. What the model wires from.
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
     * The sidechain key routed to the slot the device declared, as
     * @ref numSidechainChannels read-only channel pointers indexed from
     * @ref startSample, like @ref audio (#2329).
     *
     * Its own port, not further channels of @ref audio: where a host keeps the
     * key is the host's business, and a device that had to know carried that
     * convention in its own DSP. Null with a zero count when nothing is routed,
     * which is "no key" rather than a silent one.
     *
     * Fewer channels than the device declared is legal -- a mono source into a
     * stereo key -- so a device reads what is there rather than what it asked
     * for.
     */
    const float* const* sidechain = nullptr;
    int numSidechainChannels = 0;
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

    /// Every parameter this device describes, in slot order. Non-virtual, over
    /// the two above: a device implements nothing extra. The view yields values,
    /// because parameterInfo() builds one per call.
    auto parameters() const {
        return std::views::iota(0, std::max(0, parameterCount())) |
               std::views::transform([this](int slot) { return parameterInfo(slot); });
    }

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
