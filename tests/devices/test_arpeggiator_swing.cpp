#include <catch2/catch_test_macros.hpp>

#include "devices/ArpeggiatorTestRig.hpp"

namespace {
using Arp = magda::test::Arp;
using Rig = magda::test::ArpRig;

// 512 samples at 48 kHz, the block a host is most likely to ask for. At 120 bpm
// that is 0.0213 beats, an order below the 1/16 step the cases below run at, so
// a warped step lands in a different block from its raw position.
constexpr double kBlockSeconds = 512.0 / 48000.0;

juce::MidiMessage on(int note) {
    return juce::MidiMessage::noteOn(1, note, juce::uint8{91});
}

using Input = std::initializer_list<juce::MidiMessage>;
const Input kNothing{};
const Input kChord{on(60), on(64), on(67), on(72)};

/// Note-ons over @p blocks of contiguous playback, the first carrying @p input.
int countNoteOns(Rig& rig, int blocks, std::initializer_list<juce::MidiMessage> input) {
    int noteOns = 0;
    for (int block = 0; block < blocks; ++block) {
        auto midi = rig.run(block * kBlockSeconds,
                            block == 0 ? input : std::initializer_list<juce::MidiMessage>{}, true,
                            false, kBlockSeconds);
        for (int i = 0; i < midi.size(); ++i)
            if (midi.message(i).isNoteOn())
                ++noteOns;
    }
    return noteOns;
}
}  // namespace

TEST_CASE("Arpeggiator swings a step into a later block instead of dropping it",
          "[arpeggiator][midi][swing]") {
    Rig rig;
    rig.setRate(Arp::Rate::Sixteenth);
    rig.arp.setParameterValue(Arp::kSwing, 0.5f);

    // Swing at 50% shifts the odd steps by 0.0625 beats, three blocks on, so
    // every one of them used to be consumed unplayed (#2362). Over 0.896 beats
    // the pattern is steps at 0, 0.3125, 0.5 and 0.8125.
    CHECK(countNoteOns(rig, 42, {on(60), on(64)}) == 4);
}

TEST_CASE("Arpeggiator plays a step quantize pulls back into an earlier block",
          "[arpeggiator][midi][swing]") {
    Rig rig;
    rig.setRate(Arp::Rate::Sixteenth);
    rig.arp.quantize.store(1.0f);
    rig.arp.quantizeSub.store(16);

    // Three notes make a 0.75-beat cycle, so the quantize grid is 0.046875 and
    // the 1/16 steps do not sit on it: step 1 snaps back to 0.234375 and step 2
    // forward to 0.515625, each across a block boundary. Over 0.683 beats.
    CHECK(countNoteOns(rig, 32, {on(60), on(64), on(67)}) == 3);
}

TEST_CASE("Arpeggiator keeps a swung step inside the gap Time Bend leaves it",
          "[arpeggiator][midi][swing]") {
    Rig rig;
    rig.setRate(Arp::Rate::Sixteenth);
    // Depth -1 with a hard angle pins the first half of the cycle onto its
    // start, so steps sit on top of each other and there is no room for swing
    // to move the odd ones into.
    rig.arp.setParameterValue(Arp::kRamp, -1.0f);
    rig.arp.setParameterValue(Arp::kSwing, 1.0f);
    rig.arp.hardAngle.store(true);

    double lastPlayed = -1.0;
    for (int block = 0; block < 100; ++block) {
        const double blockStart = block * kBlockSeconds;
        auto midi = rig.run(blockStart, block == 0 ? kChord : kNothing, true, false, kBlockSeconds);
        for (int i = 0; i < midi.size(); ++i) {
            const double stamp = midi.message(i).getTimeStamp();
            REQUIRE(stamp >= 0.0);
            REQUIRE(stamp < kBlockSeconds);
            if (midi.message(i).isNoteOn()) {
                CHECK(blockStart + stamp >= lastPlayed);
                lastPlayed = blockStart + stamp;
            }
        }
    }
}
