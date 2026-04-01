#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>

namespace magda::daw::audio {

namespace te = tracktion::engine;

/**
 * @brief Base class for MIDI device plugins (arpeggiator, step sequencer, chord engine, etc.)
 *
 * Provides common plugin identity (takesMidiInput, isSynth=false, audio passthrough),
 * MIDI output note tracking for the UI note strip, and shared helpers.
 *
 * Subclasses override processMidi() to implement their specific MIDI logic.
 */
class MidiDevicePlugin : public te::Plugin {
  public:
    MidiDevicePlugin(const te::PluginCreationInfo& info);
    ~MidiDevicePlugin() override;

    // --- te::Plugin identity (shared by all MIDI devices) ---
    bool takesMidiInput() override {
        return true;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    // --- MIDI output note data for UI note strip ---
    // Written on audio thread, read on UI thread
    std::atomic<int> midiOutNote_{-1};
    std::atomic<int> midiOutVelocity_{0};

  protected:
    double sampleRate_ = 44100.0;

    // --- Helpers ---

    /** Send note-off for the last played note and clear the output note state. */
    void sendNoteOff(te::MidiMessageArray& midi, int noteNumber);

    /** Clear the MIDI output note display (no note playing). */
    void clearMidiOutDisplay();

    /** Update the MIDI output note display with a new note. */
    void setMidiOutDisplay(int noteNumber, int velocity);

    // --- te::Plugin overrides ---
    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDevicePlugin)
};

}  // namespace magda::daw::audio
