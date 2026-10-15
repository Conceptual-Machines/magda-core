#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "io/CaptureRing.hpp"
#include "transport/TempoMap.hpp"

/// @file The master's recent audio, and where KEEP finds what to take in it (#2311).

using magda::engine::CaptureRing;
using magda::engine::TempoMap;

namespace {

constexpr int kBlock = 1024;      // one meter chunk per block
constexpr double kRate = 1024.0;  // so a block is a second

juce::AudioBuffer<float> blockOf(float value) {
    juce::AudioBuffer<float> block(2, kBlock);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(block.getWritePointer(channel), value, kBlock);
    return block;
}

/// One beat per block from @p from.
void play(CaptureRing& ring, double from, int blocks, float level) {
    for (int i = 0; i < blocks; ++i)
        ring.write(blockOf(level), {from + i, from + i + 1.0}, true);
}

void stopped(CaptureRing& ring, int blocks, float level) {
    for (int i = 0; i < blocks; ++i)
        ring.write(blockOf(level), {0.0, 0.0}, false);
}

}  // namespace

TEST_CASE("A pass is the latest stretch the transport played, back to a wrap",
          "[engine][capture][2311]") {
    CaptureRing ring(2, 1 << 20, kRate);
    play(ring, 0.0, 8, 0.5f);  // beats 0-8
    play(ring, 4.0, 4, 0.5f);  // looped back to 4
    stopped(ring, 2, 0.0f);

    const auto meter = ring.meter(4);
    CHECK(meter.pass.start == 8 * kBlock);
    CHECK(meter.pass.end == 12 * kBlock);
    CHECK(meter.passBeats.start == 4.0);
    CHECK(meter.passBeats.end == 8.0);
    CHECK(meter.phrase.empty());
}

TEST_CASE("A phrase is the latest sound heard while stopped, back to a silence or a stop",
          "[engine][capture][2311]") {
    CaptureRing ring(2, 1 << 20, kRate);

    SECTION("a long enough silence starts a new phrase") {
        stopped(ring, 2, 0.5f);  // a first idea
        stopped(ring, 4, 0.0f);  // four seconds of nothing
        stopped(ring, 3, 0.5f);  // the one to keep
        stopped(ring, 2, 0.0f);  // and quiet since

        const auto meter = ring.meter(4);
        CHECK(meter.phrase.start == 6 * kBlock);
        CHECK(meter.phrase.end == 9 * kBlock);
    }

    SECTION("a short pause does not") {
        stopped(ring, 2, 0.5f);
        stopped(ring, 1, 0.0f);
        stopped(ring, 2, 0.5f);

        const auto meter = ring.meter(4);
        CHECK(meter.phrase.start == 0);
        CHECK(meter.phrase.end == 5 * kBlock);
    }

    SECTION("what sounds on past a stop is the pass's, and a phrase starts after it") {
        play(ring, 0.0, 4, 0.5f);
        stopped(ring, 2, 0.5f);  // the release
        CHECK(ring.meter(4).phrase.empty());

        stopped(ring, 1, 0.0f);
        stopped(ring, 2, 0.5f);  // played after it fell silent
        const auto meter = ring.meter(4);
        CHECK(meter.pass.end == 4 * kBlock);
        CHECK(meter.phrase.start == 7 * kBlock);
        CHECK(meter.phrase.end == 9 * kBlock);
    }
}

TEST_CASE("Audio the writer has lapped is not handed back", "[engine][capture][2311]") {
    CaptureRing ring(2, 1 << 15, kRate);  // the smallest ring the write margin allows
    stopped(ring, 128, 0.25f);            // four times round

    juce::AudioBuffer<float> audio;
    CHECK_FALSE(ring.read(0, kBlock, audio));
    const auto meter = ring.meter(1);
    CHECK(ring.read(meter.oldestReadable, kBlock, audio));
    CHECK(ring.read(meter.writtenFrames - kBlock, kBlock, audio));
    CHECK(audio.getSample(1, kBlock - 1) == 0.25f);
    CHECK(meter.phrase.start >= meter.oldestReadable);
}

TEST_CASE("N bars back counts the bar the end falls in", "[engine][capture][2311]") {
    const TempoMap fourFour({{0.0, 120.0}}, {{0.0, 4, 4}});
    CHECK(magda::engine::barsBefore(fourFour, 10.0, 1) == 8.0);  // inside bar 3
    CHECK(magda::engine::barsBefore(fourFour, 10.0, 2) == 4.0);
    CHECK(magda::engine::barsBefore(fourFour, 8.0, 1) == 4.0);  // on a bar line: the one before

    // A bar of 3/4 is three beats, 6/8 is three quarter notes.
    const TempoMap threeFour({{0.0, 120.0}}, {{0.0, 3, 4}});
    CHECK(magda::engine::barsBefore(threeFour, 7.0, 2) == 3.0);
    const TempoMap sixEight({{0.0, 120.0}}, {{0.0, 6, 8}});
    CHECK(magda::engine::barsBefore(sixEight, 7.0, 1) == 6.0);
}

TEST_CASE("The meter's peaks are the ring's level, oldest first", "[engine][capture][2311]") {
    CaptureRing ring(2, 64 * kBlock, kRate);
    stopped(ring, 48, 0.0f);
    stopped(ring, 16, 0.5f);

    const auto meter = ring.meter(4);
    REQUIRE(meter.peaks.size() == 4);
    CHECK(meter.peaks[2] == 0.0f);
    CHECK(meter.peaks[3] == 0.5f);
}

TEST_CASE("Live MIDI is kept on the audio's clock, by source", "[engine][capture][3053]") {
    CaptureRing ring(2, 1 << 20, kRate);
    stopped(ring, 3, 0.0f);  // three blocks in

    juce::MidiBuffer keyboard;
    keyboard.addEvent(juce::MidiMessage::noteOn(2, 60, static_cast<juce::uint8>(100)), 10);
    keyboard.addEvent(juce::MidiMessage::noteOff(2, 60), 2000);  // the next segment's
    juce::MidiBuffer pads;
    pads.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(90)), 500);
    const std::array<magda::engine::LiveMidiStream, 2> streams{
        magda::engine::LiveMidiStream{7, &keyboard}, magda::engine::LiveMidiStream{9, &pads}};

    // A callback of two segments, as a loop wrap would cut it.
    ring.writeMidi(streams, 0, kBlock);
    ring.write(blockOf(0.5f), {0.0, 0.0}, false);
    ring.writeMidi(streams, kBlock, kBlock);
    ring.write(blockOf(0.5f), {0.0, 0.0}, false);

    const auto events = ring.midiFrom(0);
    REQUIRE(events.size() == 3);
    CHECK(events[0].frame == 3 * kBlock + 10);
    CHECK(events[0].source == 7);
    CHECK(events[0].bytes[0] == 0x91);
    CHECK(events[0].bytes[1] == 60);
    CHECK(events[0].bytes[2] == 100);
    CHECK(events[1].frame == 3 * kBlock + 500);
    CHECK(events[1].source == 9);
    CHECK(events[2].frame == 3 * kBlock + 2000);
    CHECK((events[2].bytes[0] & 0xf0) == 0x80);

    CHECK(ring.midiFrom(3 * kBlock + 11).size() == 2);
}

TEST_CASE("MIDI the writer has lapped is not handed back", "[engine][capture][3053]") {
    CaptureRing ring(2, 1 << 20, kRate);
    juce::MidiBuffer dense;
    for (int i = 0; i < kBlock; ++i)
        dense.addEvent(juce::MidiMessage::controllerEvent(1, 1, i % 128), i);
    const std::array<magda::engine::LiveMidiStream, 1> streams{
        magda::engine::LiveMidiStream{1, &dense}};

    for (int block = 0; block < 160; ++block) {  // past the 131072 messages kept
        ring.writeMidi(streams, 0, kBlock);
        ring.write(blockOf(0.0f), {0.0, 0.0}, false);
    }

    const auto events = ring.midiFrom(0);
    CHECK(events.size() < static_cast<std::size_t>(1 << 17));
    CHECK(events.size() > static_cast<std::size_t>(1 << 16));
    CHECK(events.back().frame == 160 * kBlock - 1);
    CHECK(std::is_sorted(events.begin(), events.end(),
                         [](const auto& a, const auto& b) { return a.frame < b.frame; }));
}
