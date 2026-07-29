#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <string_view>

#include "core/ParameterInfo.hpp"

namespace magda::daw::audio {

struct DeviceProperties {
    std::string_view pluginId;
    std::string_view name;
    std::string_view shortName;
    bool takesMidiInput = false;
    bool takesAudioInput = true;
    bool isSynth = false;
    bool producesAudioWithoutInput = false;
    bool canSidechain = false;
    double latencySeconds = 0.0;
    double tailLengthSeconds = 0.0;
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
 * Mutable MIDI view supplied by a host adapter.
 *
 * Event timestamps remain in seconds. This matches MAGDA's current device
 * semantics without exposing the current engine's MIDI container.
 */
class DeviceMidiBuffer {
  public:
    virtual ~DeviceMidiBuffer() = default;

    virtual int size() const = 0;
    virtual DeviceMidiEvent event(int index) const = 0;
    virtual void setEvent(int index, DeviceMidiEvent event) = 0;
    virtual void removeEvent(int index) = 0;
    virtual void addEvent(DeviceMidiEvent event) = 0;
    virtual void clear() = 0;
    virtual void sortByTimestamp() = 0;
    virtual bool isAllNotesOff() const = 0;
    virtual void setAllNotesOff(bool allNotesOff) = 0;
};

struct DeviceProcessContext {
    juce::AudioBuffer<float>* audio = nullptr;
    DeviceMidiBuffer* midi = nullptr;
    int startSample = 0;
    int numSamples = 0;
    double midiTimeOffsetSeconds = 0.0;
    double timelineStartSeconds = 0.0;
    double timelineEndSeconds = 0.0;
    bool isPlaying = false;
    bool isScrubbing = false;
    bool isRendering = false;
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
};

}  // namespace magda::daw::audio
