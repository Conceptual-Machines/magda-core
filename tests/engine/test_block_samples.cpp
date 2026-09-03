#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "exec/RenderContext.hpp"

/**
 * @file test_block_samples.cpp
 * @brief The two things a sample offset inside a block can mean (#2336).
 *
 * An event is a sample the block plays and an edge is a bound of a stretch it
 * covers, and the difference shows at exactly one place: the boundary past the
 * block's last sample, which is a legal edge and not a sample anything sounds
 * on.
 */

using magda::engine::BlockInfo;
using magda::engine::EdgeSample;
using magda::engine::EventSample;
using magda::engine::TempoMap;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

/// 120 bpm: half a second to the beat, 24000 samples.
constexpr double kSecondsPerBeat = 0.5;

/// The block starting @p fromSample of a transport rolling from zero at one
/// tempo, with every face the clock would give it.
BlockInfo blockFrom(std::int64_t fromSample, const TempoMap* tempo = nullptr) {
    BlockInfo block;
    block.numSamples = kBlockSize;
    block.sampleRate = kSampleRate;
    block.playing = true;
    block.continuous = true;
    block.seconds.start = static_cast<double>(fromSample) / kSampleRate;
    block.seconds.end = static_cast<double>(fromSample + kBlockSize) / kSampleRate;
    block.beats.start = block.seconds.start / kSecondsPerBeat;
    block.beats.end = block.seconds.end / kSecondsPerBeat;
    block.monotonicBeats = block.beats;
    block.monotonicSeconds = block.seconds;
    block.monotonicSamples = {magda::engine::SamplePosition{fromSample},
                              magda::engine::SamplePosition{fromSample + kBlockSize}};
    block.tempo = tempo;
    return block;
}

}  // namespace

TEST_CASE("An event lands on a sample the block plays", "[engine][block][samples]") {
    const auto block = blockFrom(0);

    CHECK(block.eventForTime(block.seconds.start) == EventSample{0});
    CHECK(block.eventForBeat(block.beats.start) == EventSample{0});

    // Inside a sample rather than nearest to it: what plays at a moment is the
    // sample that moment is within.
    const auto halfWay = block.seconds.start + (100.6 / kSampleRate);
    CHECK(block.eventForTime(halfWay) == EventSample{100});
}

TEST_CASE("No event is ever placed past the block's last sample",
          "[engine][block][samples][2336]") {
    const auto block = blockFrom(0);

    // The acceptance criterion, from both faces. Nearest would round anything
    // in the last half sample to 512, which is a buffer index this block does
    // not have: a MIDI message written there reaches nobody, and a voice
    // started there starts nothing.
    const auto lastSample = block.seconds.end - (0.25 / kSampleRate);

    CHECK(block.eventForTime(lastSample) == EventSample{kBlockSize - 1});
    CHECK(block.eventForBeat(block.beats.end - 1e-9) == EventSample{kBlockSize - 1});
}

TEST_CASE("A moment on the boundary is the next block's first sample",
          "[engine][block][samples][2336]") {
    // Where the event that resolved to N goes, and it gets there without being
    // carried: the next block's stretch begins on it.
    const auto first = blockFrom(0);
    const auto second = blockFrom(kBlockSize);

    const auto boundary = first.seconds.end;
    REQUIRE(boundary == Catch::Approx(second.seconds.start));

    CHECK(second.eventForTime(boundary) == EventSample{0});
    CHECK(second.eventForBeat(second.beats.start) == EventSample{0});
}

TEST_CASE("An edge may be the boundary past the last sample", "[engine][block][samples]") {
    const auto block = blockFrom(0);

    // Which is what a half-open stretch that runs to the end of the block is,
    // and why an edge is a different type from an event.
    CHECK(block.edgeForTime(block.seconds.end) == EdgeSample{kBlockSize});
    CHECK(block.edgeForBeat(block.beats.end) == EdgeSample{kBlockSize});

    CHECK(block.edgeForTime(block.seconds.start) == EdgeSample{0});
    CHECK(block.edgeForTime(block.seconds.end) - block.edgeForTime(block.seconds.start) ==
          kBlockSize);
}

TEST_CASE("An edge that has to be heard sounds a sample early", "[engine][block][samples][2336]") {
    const auto block = blockFrom(0);

    // A note-off on the block boundary. Written at 512 it is written nowhere,
    // and the stretch that owed it has gone by the time the next block renders:
    // the note would hang.
    CHECK(block.soundsAt(block.edgeForBeat(block.beats.end)) == EventSample{kBlockSize - 1});

    // Everything short of the boundary is left where it is.
    CHECK(block.soundsAt(EdgeSample{0}) == EventSample{0});
    CHECK(block.soundsAt(EdgeSample{17}) == EventSample{17});
}

TEST_CASE("A beat is placed through the map, not along the block's line",
          "[engine][block][samples][2336]") {
    // A ramp baked into sections, and a block that spans a boundary: the line
    // between the block's two ends is not the map inside it. The transport does
    // not currently hand out such a block, so this is built by hand, and it
    // holds whatever the clock does about cutting (#2333).
    const TempoMap ramp({{0.0, 20.0, 0.0f}, {1.0, 300.0, 0.0f}}, {});

    // Long enough to cross the baked boundary at 0.75, which is where the slope
    // changes: the map cuts a ramp four ways to the beat. A block of the usual
    // few hundred samples sits inside one section, where the line is the map.
    const auto from = ramp.beatToTime(0.5);
    const auto to = ramp.beatToTime(0.85);

    BlockInfo block;
    block.numSamples = static_cast<int>(std::lround((to - from) * kSampleRate));
    block.sampleRate = kSampleRate;
    block.playing = true;
    block.seconds.start = from;
    block.seconds.end = to;
    block.beats.start = ramp.timeToBeat(from);
    block.beats.end = ramp.timeToBeat(to);
    block.tempo = &ramp;

    auto worstOnTheLine = 0.0;

    for (const auto through : {0.25, 0.5, 0.75}) {
        const auto beat = block.beats.start + (through * block.beats.length());
        const auto exact = (ramp.beatToTime(beat) - block.seconds.start) * kSampleRate;

        // Where the block's own two ends would have put it, which is what the
        // conversion used to do.
        const auto onTheLine = through * block.numSamples;
        worstOnTheLine = std::max(worstOnTheLine, std::abs(onTheLine - exact));

        // To the sample, rather than to the exact integer: which side of a
        // fractional answer a rounding rule lands on is the rule's business
        // (TimeDomains::eventAt), and asserting it here would be asserting the
        // rule twice.
        CHECK(std::abs(static_cast<double>(block.eventForBeat(beat).value) - exact) <= 1.0);
    }

    // And this is a block where the map and the line differ, or the case above
    // would pass on one that could not tell them apart.
    CHECK(worstOnTheLine > 1.0);
}

TEST_CASE("A block with beats alone places along its own line", "[engine][block][samples]") {
    // What a caller assembling a block by hand from beats gets, which is every
    // resolver test in the tree: no seconds face, no map, and the block's own
    // two beat ends as the only line there is.
    BlockInfo block;
    block.numSamples = 64;
    block.playing = true;
    block.beats = {4.0, 5.0};

    CHECK(block.eventForBeat(4.0) == EventSample{0});
    CHECK(block.eventForBeat(4.5) == EventSample{32});
    CHECK(block.eventForBeat(5.0) == EventSample{63});
    CHECK(block.edgeForBeat(5.0) == EdgeSample{64});
}
