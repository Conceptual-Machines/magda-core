#pragma once

#include <atomic>

#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

/**
 * @brief Base class for MIDI-FX MagdaDevices (arpeggiator, strum, step
 *        sequencers).
 *
 * The engine-neutral counterpart of the retired host-native MidiDevicePlugin
 * base: shared MIDI output note tracking for the UI note strip, and the
 * note-off helper. The retired base's plugin identity (takes MIDI, passes
 * audio through, not a synth) is what DeviceProperties defaults already say,
 * so each device declares its own properties() and nothing here.
 *
 * The retired base is gone: the step sequencers were its last two subclasses,
 * and they crossed in #2313.
 */
class MidiMagdaDevice : public MagdaDevice {
  public:
    // --- MIDI output note data for UI note strip ---
    // Written on audio thread, read on UI thread
    std::atomic<int> midiOutNote_{-1};
    std::atomic<int> midiOutVelocity_{0};

  protected:
    double sampleRate_ = 44100.0;

    void prepare(const DevicePrepareContext& context) override {
        sampleRate_ = context.sampleRate;
    }

    /** Send note-off for the given note (channel 1, like the retired base).
     *  The source is the one the note-on carried, so a device downstream reads
     *  the pair as one note from one origin (#2416). */
    static void sendNoteOff(DeviceMidiOutput& midi, int noteNumber, std::uint32_t sourceId = 0) {
        if (noteNumber >= 0)
            midi.addEvent({juce::MidiMessage::noteOff(1, noteNumber), sourceId});
    }

    /** Clear the MIDI output note display (no note playing). */
    void clearMidiOutDisplay() {
        midiOutNote_.store(-1, std::memory_order_relaxed);
        midiOutVelocity_.store(0, std::memory_order_relaxed);
    }

    /** Update the MIDI output note display with a new note. */
    void setMidiOutDisplay(int noteNumber, int velocity) {
        midiOutNote_.store(noteNumber, std::memory_order_relaxed);
        midiOutVelocity_.store(velocity, std::memory_order_relaxed);
    }
};

}  // namespace magda::daw::audio
