#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "TestDeviceMidiBuffer.hpp"
#include "magda/daw/audio/plugins/MidiStrumPlugin.hpp"

namespace {
namespace audio = magda::daw::audio;
using Strum = audio::MidiStrumPlugin;

constexpr double kSampleRate = 48000.0;
// 10 ms, so the 30 ms collect window and a 400 ms strum both span many blocks.
constexpr int kBlock = 480;

struct Gate {
    int note = 0;
    bool on = false;
};

struct StrumRig {
    Strum strum;
    std::uint32_t source = 7;
    /// Every gate the device has emitted, in block order.
    std::vector<Gate> gates;

    StrumRig() {
        strum.prepare({.sampleRate = kSampleRate, .maximumBlockSize = kBlock});
    }

    void setDisplay(int index, float value) {
        strum.setParameterValue(
            index, magda::ParameterUtils::realToNormalized(value, strum.parameterInfo(index)));
    }

    void run(std::initializer_list<juce::MidiMessage> input = {}, bool playing = true) {
        magda::test::DeviceMidiBuffer in;
        for (const auto& message : input)
            in.events.push_back({message, source});
        magda::test::DeviceMidiBuffer out;
        audio::DeviceProcessContext context;
        context.midiIn = &in;
        context.midiOut = &out;
        context.numSamples = kBlock;
        context.isPlaying = playing;
        strum.process(context);
        for (const auto& event : out.events) {
            if (event.message.isNoteOn())
                gates.push_back({event.message.getNoteNumber(), true});
            else if (event.message.isNoteOff())
                gates.push_back({event.message.getNoteNumber(), false});
        }
    }

    void runFor(int blocks, bool playing = true) {
        for (int i = 0; i < blocks; ++i)
            run({}, playing);
    }

    /// Net note-ons minus note-offs for a pitch: anything above zero is a note
    /// left sounding with nothing coming to release it.
    int hanging(int note) const {
        int count = 0;
        for (const auto& gate : gates)
            if (gate.note == note)
                count += gate.on ? 1 : -1;
        return count;
    }

    int gateCount(int note, bool on) const {
        int count = 0;
        for (const auto& gate : gates)
            if (gate.note == note && gate.on == on)
                ++count;
        return count;
    }
};

juce::MidiMessage on(int note) {
    return juce::MidiMessage::noteOn(1, note, juce::uint8{100});
}

juce::MidiMessage off(int note) {
    return juce::MidiMessage::noteOff(1, note);
}
}  // namespace

TEST_CASE("Strum stopped mid-pass plays no note it had not reached", "[strum][midi]") {
    StrumRig rig;
    rig.setDisplay(Strum::kStrumLength, 400.0f);
    rig.setDisplay(Strum::kOrder, static_cast<float>(Strum::Order::Up));

    rig.run({on(60), on(64), on(67)});
    rig.runFor(8);  // 30 ms collect + ~50 ms into a 400 ms strum
    const auto atStop = rig.gates.size();
    REQUIRE(atStop > 0);       // the pass started
    REQUIRE(atStop < 3 * 2u);  // and did not finish

    rig.run({}, false);
    rig.runFor(60, false);  // well past the end of the strum that was cut short

    for (size_t i = atStop; i < rig.gates.size(); ++i) {
        INFO("gate " << i << " note " << rig.gates[i].note);
        CHECK_FALSE(rig.gates[i].on);
    }
    for (const int note : {60, 64, 67}) {
        INFO("note " << note);
        CHECK(rig.hanging(note) == 0);
    }
}

TEST_CASE("Strum released mid-pass plays no note it had not reached", "[strum][midi]") {
    StrumRig rig;
    rig.setDisplay(Strum::kStrumLength, 400.0f);

    rig.run({on(60), on(64), on(67)});
    rig.runFor(9);  // ~100 ms held, a quarter of the way through the strum
    const auto atRelease = rig.gates.size();
    REQUIRE(atRelease > 0);
    REQUIRE(atRelease < 3 * 2u);

    rig.run({off(60), off(64), off(67)});
    rig.runFor(60);

    for (size_t i = atRelease; i < rig.gates.size(); ++i) {
        INFO("gate " << i << " note " << rig.gates[i].note);
        CHECK_FALSE(rig.gates[i].on);
    }
    for (const int note : {60, 64, 67}) {
        INFO("note " << note);
        CHECK(rig.hanging(note) == 0);
    }
}

TEST_CASE("Strum re-strumming a changed chord releases what it retriggers", "[strum][midi]") {
    StrumRig rig;
    rig.setDisplay(Strum::kStrumLength, 20.0f);  // the first pass lands before the change

    rig.run({on(60), on(64), on(67)});
    rig.runFor(8);
    REQUIRE(rig.gateCount(60, true) == 1);

    rig.run({on(71)});
    rig.runFor(8);

    // Every pitch the second pass replayed got a note-off first: a downstream
    // instrument that allocates per note-on keeps no hung voice.
    for (const int note : {60, 64, 67}) {
        INFO("note " << note);
        CHECK(rig.gateCount(note, true) == 2);
        CHECK(rig.gateCount(note, false) == 1);
    }
    CHECK(rig.gateCount(71, true) == 1);

    rig.run({off(60), off(64), off(67), off(71)});
    rig.runFor(4);
    for (const int note : {60, 64, 67, 71}) {
        INFO("note " << note);
        CHECK(rig.hanging(note) == 0);
    }
}

TEST_CASE("Strum loop re-strum supersedes the pass it interrupts", "[strum][midi]") {
    StrumRig rig;
    rig.setDisplay(Strum::kTrigger, static_cast<float>(Strum::Trigger::Loop));
    rig.setDisplay(Strum::kLoopSync, static_cast<float>(Strum::LoopSync::Time));
    rig.setDisplay(Strum::kSyncInterval, 60.0f);  // shorter than the strum it repeats
    rig.setDisplay(Strum::kStrumLength, 400.0f);

    rig.run({on(60), on(64), on(67)});
    rig.runFor(100);
    rig.run({off(60), off(64), off(67)});
    rig.runFor(60);

    for (const int note : {60, 64, 67}) {
        INFO("note " << note);
        CHECK(rig.hanging(note) == 0);
    }
}

TEST_CASE("Strum closes the note an Up/Down pass sounds twice", "[strum][midi]") {
    StrumRig rig;
    rig.setDisplay(Strum::kOrder, static_cast<float>(Strum::Order::UpDown));
    rig.setDisplay(Strum::kStrumLength, 80.0f);

    rig.run({on(60), on(64), on(67)});
    rig.runFor(20);

    // 60 and 67 are the ends of the walk; 64 is played on the way up and again
    // on the way down, and the second onset must retrigger, not double up.
    CHECK(rig.gateCount(64, true) == 2);
    CHECK(rig.gateCount(64, false) == 1);
    CHECK(rig.gateCount(60, true) == 1);
    CHECK(rig.gateCount(67, true) == 1);

    bool sawFirstOn = false;
    for (const auto& gate : rig.gates) {
        if (gate.note != 64)
            continue;
        if (gate.on && sawFirstOn)
            FAIL_CHECK("second note-on for 64 arrived with no note-off before it");
        if (gate.on)
            sawFirstOn = true;
        else
            sawFirstOn = false;
    }

    rig.run({off(60), off(64), off(67)});
    rig.runFor(4);
    for (const int note : {60, 64, 67}) {
        INFO("note " << note);
        CHECK(rig.hanging(note) == 0);
    }
}

TEST_CASE("Strum caps the chord it latches", "[strum][midi]") {
    StrumRig rig;
    rig.setDisplay(Strum::kOrder, static_cast<float>(Strum::Order::UpDown));
    rig.setDisplay(Strum::kStrumLength, 20.0f);

    for (int note = 24; note < 24 + 60; ++note)
        rig.run({on(note)});
    rig.runFor(20);
    REQUIRE(!rig.gates.empty());

    for (int note = 24; note < 24 + 60; ++note)
        rig.run({off(note)});
    rig.runFor(20);

    for (int note = 24; note < 24 + 60; ++note) {
        INFO("note " << note);
        CHECK(rig.hanging(note) == 0);
    }
}
