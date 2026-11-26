// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/MidiFileReader.hpp"

namespace {

constexpr int kTicksPerQuarter = 960;

/** @brief A one-track file of @p events, timestamps in beats. */
juce::File writeFile(const juce::TemporaryFile& temp,
                     std::initializer_list<std::pair<double, juce::MidiMessage>> events) {
    juce::MidiMessageSequence track;
    for (auto [beat, message] : events) {
        message.setTimeStamp(beat * kTicksPerQuarter);
        track.addEvent(message);
    }
    juce::MidiFile file;
    file.setTicksPerQuarterNote(kTicksPerQuarter);
    file.addTrack(track);
    juce::FileOutputStream output(temp.getFile());
    REQUIRE(file.writeTo(output));
    return temp.getFile();
}

}  // namespace

TEST_CASE("A MIDI file's program changes import with the bank select before them",
          "[midi][import][3412]") {
    juce::TemporaryFile temp(".mid");
    const auto file = writeFile(temp, {{0.0, juce::MidiMessage::controllerEvent(1, 0, 4)},
                                       {0.0, juce::MidiMessage::controllerEvent(1, 32, 9)},
                                       {0.0, juce::MidiMessage::programChange(1, 20)},
                                       {1.0, juce::MidiMessage::controllerEvent(1, 7, 90)},
                                       {2.0, juce::MidiMessage::programChange(1, 21)},
                                       {3.0, juce::MidiMessage::controllerEvent(1, 0, 1)}});

    const auto imported = magda::daw::readMidiTracks(file, 120.0);
    REQUIRE(imported.has_value());
    REQUIRE(imported->tracks.size() == 1);
    const auto& track = imported->tracks.front();

    REQUIRE(track.programChanges.size() == 2);
    CHECK(track.programChanges[0].program == 20);
    CHECK(track.programChanges[0].bankMsb == 4);
    CHECK(track.programChanges[0].bankLsb == 9);
    CHECK(track.programChanges[0].beatPosition == Catch::Approx(0.0));
    CHECK(track.programChanges[1].program == 21);
    CHECK(track.programChanges[1].bankMsb == -1);
    CHECK(track.programChanges[1].beatPosition == Catch::Approx(2.0));

    // Volume stays a controller, and so does a bank select no program change follows.
    REQUIRE(track.controllers.size() == 2);
    CHECK(track.controllers[0].controller == 7);
    CHECK(track.controllers[1].controller == 0);
    CHECK(track.controllers[1].beatPosition == Catch::Approx(3.0));
}

TEST_CASE("A MIDI file holding only program changes still imports a track",
          "[midi][import][3412]") {
    juce::TemporaryFile temp(".mid");
    const auto file = writeFile(temp, {{1.0, juce::MidiMessage::programChange(1, 3)}});

    const auto imported = magda::daw::readMidiTracks(file, 120.0);
    REQUIRE(imported.has_value());
    REQUIRE(imported->tracks.size() == 1);
    CHECK(imported->tracks.front().programChanges.size() == 1);
}
