// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

#include "magda/daw/core/MidiFileWriter.hpp"

namespace {

using magda::MidiNote;
using magda::daw::MidiFileWriter;

constexpr double tpq = MidiFileWriter::ticksPerQuarter;

MidiNote note(double startBeat, double lengthBeats, int noteNumber) {
    MidiNote n;
    n.startBeat = startBeat;
    n.lengthBeats = lengthBeats;
    n.noteNumber = noteNumber;
    return n;
}

/** @brief The (on, off) ticks of each note in @p seq, failing on any unpaired note-on. */
std::vector<std::pair<double, double>> pairedNotes(juce::MidiMessageSequence& seq) {
    seq.updateMatchedPairs();
    std::vector<std::pair<double, double>> pairs;
    for (int i = 0; i < seq.getNumEvents(); ++i) {
        const auto& msg = seq.getEventPointer(i)->message;
        if (!msg.isNoteOn())
            continue;
        REQUIRE(seq.getIndexOfMatchingKeyUp(i) > i);
        pairs.emplace_back(msg.getTimeStamp(), seq.getTimeOfMatchingKeyUp(i));
    }
    return pairs;
}

}  // namespace

TEST_CASE("MIDI export drops notes that end before the range start (#3279)", "[midi][export]") {
    juce::MidiMessageSequence seq;
    // The clip starts 4 beats before the range: a note on beat 0 ends 3 beats before it.
    const double maxTick =
        MidiFileWriter::appendClipInRange(seq, {note(0.0, 1.0, 60)}, {}, {}, -4.0, 8.0, 1);

    REQUIRE(seq.getNumEvents() == 0);
    REQUIRE(maxTick == 0.0);
}

TEST_CASE("MIDI export drops a note that ends exactly on the range start", "[midi][export]") {
    juce::MidiMessageSequence seq;
    MidiFileWriter::appendClipInRange(seq, {note(0.0, 2.0, 60), note(2.0, 1.0, 60)}, {}, {}, -2.0,
                                      8.0, 1);

    const auto pairs = pairedNotes(seq);
    REQUIRE(pairs.size() == 1);
    REQUIRE(pairs[0].first == 0.0);
    REQUIRE(pairs[0].second == tpq);
}

TEST_CASE("MIDI export shortens notes that cross the range edges", "[midi][export]") {
    juce::MidiMessageSequence seq;
    // Range is 4 beats; the clip starts 1 beat before it.
    const double maxTick = MidiFileWriter::appendClipInRange(
        seq, {note(0.0, 2.0, 60), note(2.0, 1.0, 62), note(4.0, 2.0, 64), note(5.0, 1.0, 65)}, {},
        {}, -1.0, 4.0, 1);

    const auto pairs = pairedNotes(seq);
    REQUIRE(pairs.size() == 3);
    REQUIRE(pairs[0].first == 0.0);
    REQUIRE(pairs[0].second == 1.0 * tpq);
    REQUIRE(pairs[1].first == 1.0 * tpq);
    REQUIRE(pairs[1].second == 2.0 * tpq);
    REQUIRE(pairs[2].first == 3.0 * tpq);
    REQUIRE(pairs[2].second == 4.0 * tpq);
    REQUIRE(maxTick == 4.0 * tpq);
}

TEST_CASE("MIDI export moves CC from before the range onto its first tick", "[midi][export]") {
    magda::MidiCCData early;
    early.controller = 1;
    early.value = 20;
    early.beatPosition = 0.0;
    magda::MidiCCData late = early;
    late.value = 90;
    late.beatPosition = 9.0;

    juce::MidiMessageSequence seq;
    MidiFileWriter::appendClipInRange(seq, {}, {early, late}, {}, -2.0, 4.0, 1);

    REQUIRE(seq.getNumEvents() == 1);
    REQUIRE(seq.getEventPointer(0)->message.getControllerValue() == 20);
    REQUIRE(seq.getEventTime(0) == 0.0);
}
