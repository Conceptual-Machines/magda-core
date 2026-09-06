#include <catch2/catch_test_macros.hpp>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/MidiChordEnginePlugin.hpp"

namespace {

namespace audio = magda::daw::audio;

/**
 * @brief Run one block of a held triad through @p engine.
 *
 * @param engine  the device under test
 * @param playing what the host says the transport is doing, which is half of
 *                the audition condition
 */
void playTriad(audio::MidiChordEnginePlugin& engine, bool playing) {
    magda::test::DeviceMidiBuffer midi;
    for (const auto note : {60, 64, 67})
        midi.events.push_back({juce::MidiMessage::noteOn(1, note, 0.8f), 1});

    magda::test::DeviceMidiBuffer out;
    audio::DeviceProcessContext context;
    context.midiIn = &midi;
    context.midiOut = &out;
    context.numSamples = 512;
    context.isPlaying = playing;

    engine.process(context);

    // A listener writes nothing: the chain's MIDI is the host's to pass on.
    CHECK(out.events.empty());
}

}  // namespace

TEST_CASE("The Chord Engine records what goes past and emits nothing", "[devices][chord]") {
    audio::MidiChordEnginePlugin engine;
    engine.prepare({.sampleRate = 44100.0, .maximumBlockSize = 512});

    CHECK(engine.properties().takesMidiInput);
    CHECK_FALSE(engine.properties().producesMidi);

    playTriad(engine, /*playing=*/true);
    CHECK(engine.getHeldNoteCount() == 3);
}

TEST_CASE("Audition off keeps playback out of the detection", "[devices][chord]") {
    audio::MidiChordEnginePlugin engine;
    engine.prepare({.sampleRate = 44100.0, .maximumBlockSize = 512});

    // The audition toggle is the chord track's own mute, pushed by the host
    // (#2314). With the transport rolling it stops the device recording, which
    // is the half of the old plugin's MIDI clear that a listener still owns:
    // the other half was silencing the track, and a muted track already is.
    engine.setChordTrackMuted(true);
    playTriad(engine, /*playing=*/true);
    CHECK(engine.getHeldNoteCount() == 0);

    // Stopped, it is authoring rather than playback, and that still detects.
    playTriad(engine, /*playing=*/false);
    CHECK(engine.getHeldNoteCount() == 3);
}
