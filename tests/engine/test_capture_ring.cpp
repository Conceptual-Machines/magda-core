#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "io/CaptureRing.hpp"
#include "transport/TempoMap.hpp"

/// @file The master's recent audio, and how the last N bars of it are found (#2311).

using magda::engine::BeatRange;
using magda::engine::CaptureRing;
using magda::engine::CaptureStamp;
using magda::engine::TempoMap;

namespace {

constexpr int kBlock = 64;

/// A block whose samples are its index, so a read says which block it came from.
juce::AudioBuffer<float> blockOf(float value) {
    juce::AudioBuffer<float> block(2, kBlock);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(block.getWritePointer(channel), value, kBlock);
    return block;
}

/// One beat per block from @p from, @p count blocks.
void play(CaptureRing& ring, double from, int count, float& value, bool playing = true) {
    for (int i = 0; i < count; ++i)
        ring.write(blockOf(value++), {from + i, from + i + (playing ? 1.0 : 0.0)}, playing);
}

}  // namespace

TEST_CASE("The latest pass ends at the last played block and starts at a wrap or a stop",
          "[engine][capture][2311]") {
    CaptureRing ring(2, 1 << 16);
    float value = 0.0f;

    play(ring, 0.0, 8, value);         // beats 0-8
    play(ring, 4.0, 4, value);         // a loop back to 4: beats 4-8 again
    play(ring, 8.0, 2, value, false);  // stopped

    const auto stamps = ring.stamps();
    const auto pass = magda::engine::latestPass(stamps);
    REQUIRE(pass.has_value());
    CHECK(pass->beats.start == 4.0);
    CHECK(pass->beats.end == 8.0);
    CHECK(pass->frames == 4 * kBlock);

    // Beat 6 is the second pass's, not the first's, which held it at an older frame.
    const auto frame = magda::engine::frameAt(stamps, *pass, 6.0);
    juce::AudioBuffer<float> audio;
    REQUIRE(ring.read(frame, kBlock, audio));
    CHECK(audio.getSample(0, 0) == 10.0f);
    CHECK(audio.getSample(1, kBlock - 1) == 10.0f);

    SECTION("nothing played is nothing to take") {
        CaptureRing silent(2, 1 << 16);
        float none = 0.0f;
        play(silent, 0.0, 4, none, false);
        CHECK_FALSE(magda::engine::latestPass(silent.stamps()).has_value());
    }
}

TEST_CASE("Audio the writer has lapped is not handed back", "[engine][capture][2311]") {
    CaptureRing ring(2, 1 << 15);  // the smallest ring the write margin allows
    float value = 0.0f;
    play(ring, 0.0, 2048, value);  // four times round

    const auto stamps = ring.stamps();
    REQUIRE_FALSE(stamps.empty());
    juce::AudioBuffer<float> audio;
    CHECK_FALSE(ring.read(0, kBlock, audio));
    CHECK(ring.read(stamps.front().frame, kBlock, audio));
    CHECK(ring.read(stamps.back().frame, kBlock, audio));
    CHECK(audio.getSample(0, 0) == 2047.0f);
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
