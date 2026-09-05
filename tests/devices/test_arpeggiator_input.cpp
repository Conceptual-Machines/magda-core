#include <catch2/catch_test_macros.hpp>

#include "devices/ArpeggiatorTestRig.hpp"

namespace {
using magda::test::ArpMidiBuffer;
using Rig = magda::test::ArpRig;

void checkOff(const ArpMidiBuffer& midi, int index = 0) {
    magda::test::checkNoteOff(midi, index);
}
}  // namespace

TEST_CASE("Arpeggiator latch replacement preserves the old note-off and new input",
          "[arpeggiator][midi][input]") {
    Rig rig(true);
    rig.startNote();
    REQUIRE(rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)}).size() == 0);

    auto midi = rig.run(0.5, {juce::MidiMessage::noteOn(1, 67, juce::uint8{73})});
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
    CHECK(midi.message(1).getVelocity() == 73);
}

TEST_CASE("Arpeggiator upstream panic stops its sounding note exactly once",
          "[arpeggiator][midi][input]") {
    for (int controller : {120, 123}) {
        INFO("controller " << controller);
        Rig rig;
        rig.startNote();
        auto midi = rig.run(0.05, {juce::MidiMessage::controllerEvent(1, controller, 0)});
        REQUIRE(midi.size() == 1);
        checkOff(midi);
        CHECK(rig.run(0.1).size() == 0);
        CHECK(rig.run(0.5).size() == 0);
    }
}

TEST_CASE("Arpeggiator input resets retain a single pending note-off",
          "[arpeggiator][midi][input]") {
    Rig rig(true);
    rig.startNote();
    rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)});
    auto midi = rig.run(0.5, {juce::MidiMessage::noteOn(1, 64, juce::uint8{80}),
                              juce::MidiMessage::allNotesOff(1), juce::MidiMessage::allSoundOff(1),
                              juce::MidiMessage::noteOn(1, 67, juce::uint8{73})});
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
    CHECK(midi.message(1).getVelocity() == 73);
}

TEST_CASE("Arpeggiator buffer panic stops its note and keeps the latched chord",
          "[arpeggiator][midi][input]") {
    Rig rig(true);
    rig.startNote();
    rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)});
    auto midi = rig.run(0.1, {}, true, true);
    CHECK(midi.isAllNotesOff());
    REQUIRE(midi.size() == 1);
    checkOff(midi);
    // The latch is the arp's own memory of a chord, and a host relocating is
    // not the player letting go, so the walk re-anchors and plays it again.
    auto next = rig.run(0.5);
    CHECK_FALSE(next.isAllNotesOff());
    REQUIRE(next.size() == 1);
    CHECK(next.message(0).isNoteOn());
    CHECK(next.message(0).getNoteNumber() == 60);
}

TEST_CASE("Arpeggiator buffer panic permits fresh input without duplicate note-offs",
          "[arpeggiator][midi][input]") {
    Rig rig(true);
    rig.startNote();
    auto midi = rig.run(
        0.5, {juce::MidiMessage::allNotesOff(1), juce::MidiMessage::noteOn(1, 67, juce::uint8{73})},
        true, true);
    CHECK(midi.isAllNotesOff());
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
    CHECK(midi.message(1).getVelocity() == 73);
}

TEST_CASE("Arpeggiator forwards buffer panic even with no sounding note",
          "[arpeggiator][midi][input]") {
    Rig rig;
    auto midi = rig.run(0.0, {}, false, true);
    CHECK(midi.isAllNotesOff());
    CHECK(midi.size() == 0);
    CHECK_FALSE(rig.run(0.05, {}, false).isAllNotesOff());
}

TEST_CASE("Arpeggiator replacing an unlatched chord stops the previous note",
          "[arpeggiator][midi][input]") {
    Rig rig;
    rig.startNote();
    auto midi = rig.run(0.5, {juce::MidiMessage::noteOff(1, 60),
                              juce::MidiMessage::noteOn(1, 67, juce::uint8{73})});
    REQUIRE(midi.size() == 2);
    checkOff(midi);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getNoteNumber() == 67);
}

TEST_CASE("Arpeggiator release and transport stop still send one note-off",
          "[arpeggiator][midi][input]") {
    Rig rig;
    rig.startNote();
    ArpMidiBuffer midi;
    SECTION("release") {
        midi = rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)});
    }
    SECTION("transport stop") {
        midi = rig.run(0.05, {}, false);
    }
    REQUIRE(midi.size() == 1);
    checkOff(midi);
    CHECK(rig.run(0.1, {}, false).size() == 0);
}
