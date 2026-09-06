#include <array>
#include <catch2/catch_test_macros.hpp>

#include "devices/ArpeggiatorTestRig.hpp"

// What the arpeggiator passes on rather than consumes (#2417).
//
// Notes are the device's material. Everything else the channel carries is
// addressed to the instrument behind it, and reaches it nowhere else: MIDI
// thru would bring the held chord back with it.

namespace {
using magda::test::Arp;
using magda::test::DeviceMidiBuffer;
using Rig = magda::test::ArpRig;

juce::MidiMessage at(juce::MidiMessage message, double timeStamp) {
    message.setTimeStamp(timeStamp);
    return message;
}
}  // namespace

TEST_CASE("Arpeggiator forwards the non-note traffic its notes displace",
          "[arpeggiator][midi][passthru][2417]") {
    Rig rig;
    auto midi = rig.run(0.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8{91}),
                              juce::MidiMessage::controllerEvent(1, 1, 64),
                              juce::MidiMessage::controllerEvent(1, 11, 100),
                              juce::MidiMessage::controllerEvent(1, 64, 127),
                              juce::MidiMessage::pitchWheel(1, 4096),
                              juce::MidiMessage::channelPressureChange(1, 55),
                              juce::MidiMessage::aftertouchChange(1, 60, 33),
                              juce::MidiMessage::programChange(1, 12)});

    // Seven forwarded, then the note the arp generated for the chord it was
    // handed: everything arrived at the top of the block, and a controller
    // coincident with a note goes out ahead of it.
    REQUIRE(midi.size() == 8);

    CHECK(midi.message(0).isController());
    CHECK(midi.message(0).getControllerNumber() == 1);
    CHECK(midi.message(0).getControllerValue() == 64);
    CHECK(midi.message(1).getControllerNumber() == 11);
    CHECK(midi.message(2).getControllerNumber() == 64);
    CHECK(midi.message(2).getControllerValue() == 127);
    CHECK(midi.message(3).isPitchWheel());
    CHECK(midi.message(3).getPitchWheelValue() == 4096);
    CHECK(midi.message(4).isChannelPressure());
    CHECK(midi.message(4).getChannelPressureValue() == 55);
    CHECK(midi.message(5).isAftertouch());
    CHECK(midi.message(5).getNoteNumber() == 60);
    CHECK(midi.message(5).getAfterTouchValue() == 33);
    CHECK(midi.message(6).isProgramChange());
    CHECK(midi.message(6).getProgramChangeNumber() == 12);
    CHECK(midi.message(7).isNoteOn());

    // Provenance travels with the message: a device behind this one reads a
    // forwarded controller the way it reads the notes beside it (#2416).
    for (int index = 0; index < 7; ++index)
        CHECK(midi.events[static_cast<std::size_t>(index)].sourceId == rig.source);
}

TEST_CASE("Arpeggiator forwards non-note messages on the channel it plays on",
          "[arpeggiator][midi][passthru][2417]") {
    // The arp collapses every input channel onto channel 1, so a controller
    // left on its own channel would address an instrument voice that is not
    // there.
    Rig rig;
    auto midi = rig.run(0.0, {juce::MidiMessage::noteOn(7, 60, juce::uint8{91}),
                              juce::MidiMessage::controllerEvent(7, 74, 90)});

    REQUIRE(midi.size() == 2);
    CHECK(midi.message(0).isController());
    CHECK(midi.message(0).getControllerNumber() == 74);
    CHECK(midi.message(0).getChannel() == 1);
    CHECK(midi.message(1).isNoteOn());
    CHECK(midi.message(1).getChannel() == 1);
}

TEST_CASE("Arpeggiator does not echo the notes it consumes",
          "[arpeggiator][midi][passthru][2417]") {
    Rig rig;
    rig.startNote();

    // One note-off, the arp closing what it started. A second would be the
    // input's own, which the pattern already replaced.
    auto midi = rig.run(0.05, {juce::MidiMessage::noteOff(1, 60)});
    REQUIRE(midi.size() == 1);
    magda::test::checkNoteOff(midi);
}

TEST_CASE("Arpeggiator drops what it cannot forward without allocating",
          "[arpeggiator][midi][passthru][2417]") {
    // SysEx addresses a device rather than the notes, and copying one would
    // put a heap allocation on the audio thread. Clock and start belong to the
    // transport, which is not this port's traffic either.
    const std::array<std::uint8_t, 6> payload{0x7d, 1, 2, 3, 4, 5};

    Rig rig;
    auto midi = rig.run(0.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8{91}),
                              juce::MidiMessage::createSysExMessage(
                                  payload.data(), static_cast<int>(payload.size())),
                              juce::MidiMessage::midiClock(), juce::MidiMessage::midiStart()});

    REQUIRE(midi.size() == 1);
    CHECK(midi.message(0).isNoteOn());
}

TEST_CASE("Arpeggiator merges forwarded traffic into its output in timestamp order",
          "[arpeggiator][midi][passthru][2417]") {
    // A block long enough to hold four sixteenths, so the forwarded messages
    // have generated notes on both sides of them rather than only after.
    Rig rig;
    rig.setRate(Arp::Rate::Sixteenth);
    auto midi = rig.run(0.0,
                        {juce::MidiMessage::noteOn(1, 60, juce::uint8{91}),
                         at(juce::MidiMessage::controllerEvent(1, 1, 20), 0.2),
                         at(juce::MidiMessage::controllerEvent(1, 1, 30), 0.25),
                         at(juce::MidiMessage::controllerEvent(1, 1, 40), 0.3)},
                        true, false, 0.5);

    REQUIRE(midi.size() > 6);
    for (int index = 1; index < midi.size(); ++index)
        CHECK(midi.message(index - 1).getTimeStamp() <= midi.message(index).getTimeStamp());

    int controllers = 0;
    for (int index = 0; index < midi.size(); ++index) {
        if (!midi.message(index).isController())
            continue;
        ++controllers;

        // The one coincident with a step is ahead of the notes at that
        // instant, the way it would be if the arp were not in the chain.
        if (midi.message(index).getControllerValue() == 30) {
            REQUIRE(index + 1 < midi.size());
            CHECK(midi.message(index).getTimeStamp() == 0.25);
            CHECK(midi.message(index + 1).getTimeStamp() == 0.25);
            CHECK(!midi.message(index + 1).isController());
        }
    }
    CHECK(controllers == 3);
}

TEST_CASE("Arpeggiator forwards the reset it acts on", "[arpeggiator][midi][passthru][2417]") {
    // CC120 and CC123 stop this device, and the instrument behind it is owed
    // the same instruction: the arp's own note-off releases what it started
    // and nothing else.
    for (int controller : {120, 123}) {
        INFO("controller " << controller);
        Rig rig;
        rig.startNote();

        auto midi = rig.run(0.05, {juce::MidiMessage::controllerEvent(1, controller, 0)});
        REQUIRE(midi.size() == 2);
        CHECK(midi.message(0).isController());
        CHECK(midi.message(0).getControllerNumber() == controller);
        magda::test::checkNoteOff(midi, 1);

        CHECK(rig.run(0.1).size() == 0);
    }
}

TEST_CASE("Arpeggiator passes a buffer panic on beside what it forwards",
          "[arpeggiator][midi][passthru][2417]") {
    // The flag is the CC-less twin of the reset above, and the two travel the
    // same way: the panic on the buffer, the pedal as an event (#2418).
    Rig rig;
    rig.liveSourceIds = {rig.source};
    rig.startNote();

    auto midi = rig.run(0.05, {at(juce::MidiMessage::controllerEvent(1, 64, 127), 0.01)}, true,
                        /*panic=*/true);

    CHECK(midi.allNotesOff);
    REQUIRE(midi.size() == 2);
    magda::test::checkNoteOff(midi, 0);
    CHECK(midi.message(1).isController());
    CHECK(midi.message(1).getControllerNumber() == 64);
    CHECK(midi.message(1).getTimeStamp() == 0.01);
}
