#include <catch2/catch_test_macros.hpp>

#include "devices/ArpeggiatorTestRig.hpp"

namespace {
using magda::test::checkNoteOff;
using Arp = magda::test::Arp;
using Rig = magda::test::ArpRig;

juce::MidiMessage on(int note) {
    return juce::MidiMessage::noteOn(1, note, juce::uint8{91});
}
}  // namespace

TEST_CASE("Arpeggiator re-anchors on a loop wrap no host flags", "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.startNote();
    // Contiguous playback up to the loop end, with the note still sounding.
    for (int block = 1; block < 10; ++block)
        rig.run(block * 0.05);

    auto midi = rig.run(0.0);
    CHECK_FALSE(midi.isAllNotesOff());
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);
}

TEST_CASE("Arpeggiator seeking far ahead lands on the new grid", "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.setRate(Arp::Rate::ThirtySecond);
    auto first = rig.run(0.0, {on(60), on(64), on(67)});
    REQUIRE(first.size() == 1);
    CHECK(first.message(0).isNoteOn());

    // Ten minutes on at 1/32 is some 9600 steps away.
    auto midi = rig.run(600.0);
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);
    CHECK(midi.message(1).getTimeStamp() == 0.0);
}

TEST_CASE("Arpeggiator keeps live keys across a transport stop", "[arpeggiator][midi][transport]") {
    Rig rig;
    // Played with the transport stopped, so this source is not clip playback.
    REQUIRE(rig.run(0.0, {on(60)}, false).size() == 1);
    rig.run(0.0, {}, true);

    auto midi = rig.run(0.05, {}, false);
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);

    // The key releases the arp the way it would have before the stop.
    rig.run(0.0, {juce::MidiMessage::noteOff(1, 60)}, false);
    for (int block = 0; block < 20; ++block)
        CHECK(rig.run(0.0, {}, false).size() == 0);
}

TEST_CASE("Arpeggiator stopping drops held notes it cannot prove came from a player",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    // Only ever seen while playing, so it may be a clip whose note-off will
    // never arrive.
    rig.startNote();

    auto midi = rig.run(0.05, {}, false);
    REQUIRE(midi.size() == 1);
    checkNoteOff(midi, 0);
    for (int block = 0; block < 20; ++block)
        CHECK(rig.run(0.0, {}, false).size() == 0);
}

TEST_CASE("Arpeggiator keeps live keys when the transport starts under a panic",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    REQUIRE(rig.run(0.0, {on(60)}, false).size() == 1);

    auto midi = rig.run(0.0, {}, true, true);
    CHECK(midi.isAllNotesOff());
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);
}

TEST_CASE("Arpeggiator locating under a panic keeps a chord proven live",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    REQUIRE(rig.run(0.0, {on(60)}, false).size() == 1);
    rig.run(0.0, {}, true);

    auto midi = rig.run(4.0, {}, true, true);
    CHECK(midi.isAllNotesOff());
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);
}

TEST_CASE("Arpeggiator locating out of a clip note does not retrigger it",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.startNote();

    // The host re-asserts what sounds at the destination and says nothing
    // about what only sounded at the old position, so the note goes with it.
    auto midi = rig.run(4.0, {}, true, true);
    CHECK(midi.isAllNotesOff());
    REQUIRE(midi.size() == 1);
    checkNoteOff(midi, 0);
    for (int block = 0; block < 10; ++block)
        CHECK(rig.run(4.05 + block * 0.05).size() == 0);
}

TEST_CASE("Arpeggiator plays what the host re-asserts under a panic",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.startNote();

    auto midi = rig.run(4.0, {on(64)}, true, true);
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 64);
}
