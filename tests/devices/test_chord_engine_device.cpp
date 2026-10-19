#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <set>

#include "DeviceTestBlock.hpp"
#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/MidiChordEnginePlugin.hpp"
#include "magda/daw/music/ChordEngine.hpp"
#include "magda/daw/music/ChordSuggestionEngine.hpp"

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
    juce::AudioBuffer<float> contextAudio(1, 512);
    contextAudio.clear();
    magda::test::DeviceTestBlock contextBlock(contextAudio);
    auto& context = contextBlock.context;
    context.midiIn = &midi;
    context.midiOut = &out;
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

TEST_CASE("A chord name's octave does not change its root", "[chord]") {
    using magda::music::ChordEngine;
    using magda::music::ChordQuality;
    using magda::music::ChordRoot;

    // Chord-track names carry the octave ("A4 min"); it read as an unknown root, so C.
    const auto spelled = ChordEngine::parseChordName("A4 min");
    CHECK(spelled.root == ChordRoot::A);
    CHECK(spelled.quality == ChordQuality::Minor);
    CHECK(ChordEngine::parseChordName("F#3 maj").root == ChordRoot::FSharp);
    CHECK(ChordEngine::parseChordName("G 7").root == ChordRoot::G);
}

TEST_CASE("Scale filtering keeps suggestions inside the chosen scale", "[chord]") {
    using magda::music::ChordEngine;
    using magda::music::ChordSuggestionEngine;

    ChordSuggestionEngine engine;
    auto& chords = ChordEngine::getInstance();
    const std::vector<magda::music::Chord> recent{
        chords.buildChordInRootPosition(magda::music::ChordRoot::C,
                                        magda::music::ChordQuality::Major),
        chords.buildChordInRootPosition(magda::music::ChordRoot::A,
                                        magda::music::ChordQuality::Minor)};

    ChordSuggestionEngine::SuggestionParams params;
    params.novelty = 0.3f;
    params.add7ths = false;

    // C major pentatonic: no F, no B.
    const std::set<int> pentatonic{0, 2, 4, 7, 9};
    const auto outsidePentatonic =
        [&](const std::vector<ChordSuggestionEngine::SuggestionItem>& s) {
            return std::ranges::count_if(s, [&](const auto& item) {
                return std::ranges::any_of(item.chord.notes, [&](const auto& note) {
                    return !pentatonic.contains(note.noteNumber % 12);
                });
            });
        };

    params.useScaleFiltering = false;
    const auto unfiltered = engine.generateSuggestions(recent, params, "C", "major");
    REQUIRE(outsidePentatonic(unfiltered) > 0);

    params.useScaleFiltering = true;
    params.explicitScalePitchClasses = pentatonic;
    const auto filtered = engine.generateSuggestions(recent, params, "C", "major");
    CHECK_FALSE(filtered.empty());
    CHECK(outsidePentatonic(filtered) == 0);

    // Past 40% novelty the filter lets the non-diatonic chords through, past 80% everything.
    params.novelty = 0.9f;
    CHECK(outsidePentatonic(engine.generateSuggestions(recent, params, "C", "major")) > 0);
}
