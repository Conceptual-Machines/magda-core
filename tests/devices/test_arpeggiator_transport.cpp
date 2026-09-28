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
    rig.liveSourceIds = {rig.source};
    // First pressed with the transport already rolling, which is the case that
    // no amount of watching the input can tell apart from a clip.
    rig.startNote();

    auto midi = rig.run(0.05, {}, false);
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);

    auto released = rig.run(0.0, {juce::MidiMessage::noteOff(1, 60)}, false);
    REQUIRE(released.size() == 1);
    checkNoteOff(released, 0);
    for (int block = 0; block < 20; ++block)
        CHECK(rig.run(0.0, {}, false).size() == 0);
}

TEST_CASE("Arpeggiator keeps a live key a clip is doubling", "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.liveSourceIds = {rig.source};
    rig.startNote();

    // A clip lands on the pitch already under the player's finger. The pattern
    // holds one note either way, but two holders now have it.
    const auto live = rig.source;
    rig.source = live + 1;
    rig.run(0.05, {on(60)});
    rig.source = live;

    auto midi = rig.run(0.1, {}, false);
    REQUIRE(midi.size() == 2);
    checkNoteOff(midi, 0);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 60);
}

TEST_CASE("Arpeggiator drops a clip note a live key of another pitch does not hold",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.liveSourceIds = {rig.source + 1};
    rig.startNote();

    rig.source = rig.source + 1;
    rig.run(0.05, {on(67)});

    auto midi = rig.run(0.1, {}, false);
    REQUIRE(midi.size() >= 1);
    // Only the live key is left, so the pattern has one note and it is not 60.
    for (int block = 0; block < 12; ++block) {
        auto next = rig.run(0.0, {}, false);
        for (int i = 0; i < next.size(); ++i)
            CHECK(next.message(i).getNoteNumber() == 67);
    }
}

TEST_CASE("Arpeggiator keeps a pitch two live inputs hold when one lets go",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    const auto first = rig.source;
    const auto second = first + 1;
    rig.liveSourceIds = {first, second};
    rig.startNote();
    rig.source = second;
    rig.run(0.05, {on(60)});

    rig.run(0.1, {}, false);
    rig.run(0.0, {juce::MidiMessage::noteOff(1, 60)}, false);

    // One input let go of the pitch; the other still has it down.
    bool played = false;
    for (int block = 0; block < 12 && !played; ++block) {
        auto midi = rig.run(0.0, {}, false);
        played = midi.size() > 0 && midi.message(midi.size() - 1).isNoteOn();
    }
    CHECK(played);
}

TEST_CASE("Arpeggiator stamps what it plays with the provenance it was given",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    rig.liveSourceIds = {rig.source};
    auto midi = rig.run(0.0, {on(60)});
    REQUIRE(midi.size() == 1);
    CHECK(midi.message(0).isNoteOn());
    CHECK(midi.events[0].sourceId == rig.source);

    // The note-off closing it says the same, so a device behind this one reads
    // one note from one origin.
    auto stopped = rig.run(0.05, {}, false);
    REQUIRE(stopped.size() >= 1);
    CHECK(stopped.message(0).isNoteOff());
    CHECK(stopped.events[0].sourceId == rig.source);

    // A clip's note keeps its own origin rather than being flattened to none.
    Rig fromClip;
    auto clipMidi = fromClip.run(0.0, {on(60)});
    REQUIRE(clipMidi.size() == 1);
    CHECK(clipMidi.events[0].sourceId == fromClip.source);
}

TEST_CASE("Arpeggiator stopping drops held notes the host does not call live input",
          "[arpeggiator][midi][transport]") {
    Rig rig;
    // A live input exists, but this phrase is not coming from it.
    rig.liveSourceIds = {rig.source + 1};
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
    rig.liveSourceIds = {rig.source};
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
    rig.liveSourceIds = {rig.source};
    rig.startNote();

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
    rig.liveSourceIds = {rig.source + 1};
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
