#include <catch2/catch_test_macros.hpp>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

// All-notes-off reaches a compiled instrument beside the events rather than
// among them, because a juce::MidiBuffer has nowhere to put it (#2418). The
// instrument walked the events alone, so every panic the host raised was
// dropped and whatever was sounding stayed sounding: a playhead jump, a mute,
// or the routing change that takes a source track's input away (#2612).

using magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

namespace {

constexpr int kBlockSize = 256;
constexpr double kSampleRate = 44100.0;

/// Renders one block and answers with the loudest sample in it.
float peakOf(MagdaPolySynthCompiledPlugin& synth, magda::test::DeviceMidiBuffer& midi) {
    juce::AudioBuffer<float> audio(2, kBlockSize);
    audio.clear();

    magda::test::DeviceMidiBuffer out;
    magda::daw::audio::DeviceProcessContext context;
    context.audio = &audio;
    context.midiIn = &midi;
    context.midiOut = &out;
    context.numSamples = kBlockSize;
    context.isPlaying = true;

    synth.process(context);
    return audio.getMagnitude(0, kBlockSize);
}

/// A synth with a note held down, and the peak that note is sounding at.
float soundOn(MagdaPolySynthCompiledPlugin& synth) {
    synth.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlockSize});

    magda::test::DeviceMidiBuffer midi;
    midi.events.push_back({juce::MidiMessage::noteOn(1, 60, 0.9f), 0});
    peakOf(synth, midi);

    // Let the envelope open before anything is measured against it.
    magda::test::DeviceMidiBuffer quiet;
    float peak = 0.0f;
    for (int block = 0; block < 8; ++block)
        peak = peakOf(synth, quiet);
    return peak;
}

/// What the synth settles to over enough blocks for a release to finish.
float decayOver(MagdaPolySynthCompiledPlugin& synth, int blocks) {
    magda::test::DeviceMidiBuffer quiet;
    float peak = 0.0f;
    for (int block = 0; block < blocks; ++block)
        peak = peakOf(synth, quiet);
    return peak;
}

}  // namespace

TEST_CASE("A compiled instrument lets go when the host raises all-notes-off",
          "[devices][compiled][2418]") {
    MagdaPolySynthCompiledPlugin synth;

    const auto sounding = soundOn(synth);
    REQUIRE(sounding > 0.0f);

    // Still held: nothing has told it to stop, so it keeps sounding.
    REQUIRE(decayOver(synth, 8) > 0.0f);

    // The panic, beside the events and with no note-off among them.
    magda::test::DeviceMidiBuffer panic;
    panic.allNotesOff = true;
    peakOf(synth, panic);

    CHECK(decayOver(synth, 200) < sounding * 0.01f);
}

TEST_CASE("A compiled instrument lets go for controller 123 as well", "[devices][compiled][2418]") {
    // The same message in band, which the Faust poly engine already answers in
    // ctrlChange. Pinned rather than fixed: the two routes to a panic have to
    // end the same way, and only the out-of-band one was broken.
    MagdaPolySynthCompiledPlugin synth;

    const auto sounding = soundOn(synth);
    REQUIRE(sounding > 0.0f);

    magda::test::DeviceMidiBuffer panic;
    panic.events.push_back({juce::MidiMessage::allNotesOff(1), 0});
    peakOf(synth, panic);

    CHECK(decayOver(synth, 200) < sounding * 0.01f);
}
