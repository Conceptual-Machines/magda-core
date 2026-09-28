#pragma once

#include <juce_core/juce_core.h>

#include <utility>

namespace magda {

/**
 * @brief Real-time MIDI note event (note on/off)
 *
 * Used for MIDI monitoring and visualization, not for sequencing.
 * For sequenced notes, see MidiNote in ClipInfo.hpp
 */
struct MidiNoteEvent {
    int noteNumber = 0;      // 0-127 (middle C = 60)
    int velocity = 0;        // 0-127 (note-off has velocity 0)
    bool isNoteOn = false;   // true = note on, false = note off
    double timestamp = 0.0;  // Time in seconds (for visualization)

    MidiNoteEvent() = default;

    MidiNoteEvent(int note, int vel, bool on, double time = 0.0)
        : noteNumber(note), velocity(vel), isNoteOn(on), timestamp(time) {}
};

/**
 * @brief MIDI Control Change (CC) event
 */
struct MidiCCEvent {
    int controller = 0;  // 0-127 (1=mod wheel, 7=volume, 10=pan, etc.)
    int value = 0;       // 0-127
    double timestamp = 0.0;

    MidiCCEvent() = default;

    MidiCCEvent(int cc, int val, double time = 0.0) : controller(cc), value(val), timestamp(time) {}
};

/**
 * @brief MIDI device information
 */
struct MidiDeviceInfo {
    juce::String id;          // Unique identifier (use for routing)
    juce::String name;        // Display name (user-friendly)
    bool isEnabled = false;   // Currently receiving MIDI
    bool isAvailable = true;  // Device is connected/present

    MidiDeviceInfo() = default;

    MidiDeviceInfo(juce::String deviceId, juce::String deviceName, bool enabled = false,
                   bool available = true)
        : id(std::move(deviceId)),
          name(std::move(deviceName)),
          isEnabled(enabled),
          isAvailable(available) {}
};

}  // namespace magda
