#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "clip/MidiEventList.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipOperations.hpp"

using Catch::Approx;
using magda::ClipInfo;
using magda::ClipOperations;

namespace {

constexpr double kBpm = 120.0;

ClipInfo midiClip(double startBeat, double lengthBeats) {
    ClipInfo clip;
    clip.setMidiContent();
    clip.setPlacementBeats(startBeat, lengthBeats);
    return clip;
}

// The fold ClipSnapshotCompiler hands the engine for this clip.
magda::engine::MidiFold foldOf(const ClipInfo& clip) {
    magda::engine::MidiFold fold;
    fold.clipStartBeat = clip.placement.startBeat;
    fold.trimOffsetBeats = std::max(0.0, clip.midiTrimOffset);
    fold.offsetBeats = clip.midiOffset;
    fold.loopEnabled = clip.loopEnabled && clip.loopLengthBeats > 0.0;
    fold.loopStartBeats = clip.loopStartBeats;
    fold.loopLengthBeats = clip.loopLengthBeats;
    return fold;
}

double engineContentBeat(const ClipInfo& clip, double timelineBeat) {
    magda::engine::MidiFoldPass passes[magda::engine::kMaxFoldPassesPerBlock];
    const int count = magda::engine::foldBlock(foldOf(clip), timelineBeat, timelineBeat + 1.0e-6,
                                               clip.placement.endBeat(), passes);
    REQUIRE(count >= 1);
    return passes[0].contentStart;
}

void requireEditorFollowsEngine(const ClipInfo& clip) {
    for (double beat = clip.placement.startBeat; beat < clip.placement.endBeat(); beat += 0.37) {
        const auto content = ClipOperations::contentBeatAtTimelineBeat(clip, beat, kBpm);
        REQUIRE(content.has_value());
        REQUIRE(*content == Approx(engineContentBeat(clip, beat)).margin(1.0e-6));
        REQUIRE(ClipOperations::timelineBeatForContentBeat(clip, *content, beat, kBpm) ==
                Approx(beat).margin(1.0e-6));
    }
}

}  // namespace

TEST_CASE("Editor playhead finds a looped clip placed after a gap (#2679)",
          "[pianoroll][playhead]") {
    auto clip = midiClip(20.0, 16.0);
    clip.loopEnabled = true;
    clip.loopLengthBeats = 4.0;

    // Past the clip's own length in timeline beats, which is where the playhead used to vanish.
    const auto content = ClipOperations::contentBeatAtTimelineBeat(clip, 27.0, kBpm);
    REQUIRE(content.has_value());
    REQUIRE(*content == Approx(3.0));

    REQUIRE_FALSE(ClipOperations::contentBeatAtTimelineBeat(clip, 18.0, kBpm).has_value());
    REQUIRE_FALSE(ClipOperations::contentBeatAtTimelineBeat(clip, 36.5, kBpm).has_value());
}

TEST_CASE("Editor playhead follows the engine fold", "[pianoroll][playhead]") {
    SECTION("looped, with a loop start and an offset") {
        auto clip = midiClip(20.0, 14.0);
        clip.loopEnabled = true;
        clip.loopStartBeats = 1.0;
        clip.loopLengthBeats = 3.0;
        clip.midiOffset = 1.5;
        requireEditorFollowsEngine(clip);
    }

    SECTION("not looped, trimmed and offset") {
        auto clip = midiClip(12.0, 8.0);
        clip.midiTrimOffset = 2.0;
        clip.midiOffset = 0.5;
        requireEditorFollowsEngine(clip);
    }
}

TEST_CASE("Session playhead fold is independent of Arrangement placement",
          "[pianoroll][playhead][session]") {
    auto clip = midiClip(20.0, 4.0);
    clip.view = magda::ClipView::Session;

    REQUIRE_FALSE(ClipOperations::contentBeatAtSessionBeat(clip, -0.01, kBpm).has_value());

    SECTION("a cycle longer than the placement keeps mapping elapsed beats") {
        clip.loopEnabled = true;
        clip.loopStartBeats = 2.0;
        clip.loopLengthBeats = 16.0;
        clip.midiOffset = 1.5;

        const auto content = ClipOperations::contentBeatAtSessionBeat(clip, 8.0, kBpm);
        REQUIRE(content.has_value());
        REQUIRE(*content == Approx(11.5));

        // Arrangement mapping keeps rejecting the same absolute position: its
        // four-beat placement still ends at beat 24.
        REQUIRE_FALSE(ClipOperations::contentBeatAtTimelineBeat(clip, 28.0, kBpm).has_value());
    }

    SECTION("a shorter cycle wraps with the same loop start and MIDI offset") {
        clip.loopEnabled = true;
        clip.loopStartBeats = 1.0;
        clip.loopLengthBeats = 2.0;
        clip.midiOffset = 0.5;

        const auto content = ClipOperations::contentBeatAtSessionBeat(clip, 5.0, kBpm);
        REQUIRE(content.has_value());
        REQUIRE(*content == Approx(2.5));
    }

    SECTION("a non-looped position uses trim and offset without placement start") {
        clip.loopEnabled = false;
        clip.midiTrimOffset = 2.0;
        clip.midiOffset = 0.5;

        const auto content = ClipOperations::contentBeatAtSessionBeat(clip, 3.0, kBpm);
        REQUIRE(content.has_value());
        REQUIRE(*content == Approx(5.5));
        REQUIRE_FALSE(ClipOperations::contentBeatAtSessionBeat(clip, 4.01, kBpm).has_value());
    }
}

TEST_CASE("A seek into a looped clip stays in the pass the transport is in",
          "[pianoroll][playhead]") {
    auto clip = midiClip(20.0, 16.0);
    clip.loopEnabled = true;
    clip.loopLengthBeats = 4.0;

    REQUIRE(ClipOperations::timelineBeatForContentBeat(clip, 1.0, 29.5, kBpm) == Approx(29.0));
    // Before the clip the first pass answers; past its end, the last whole one.
    REQUIRE(ClipOperations::timelineBeatForContentBeat(clip, 1.0, 5.0, kBpm) == Approx(21.0));
    REQUIRE(ClipOperations::timelineBeatForContentBeat(clip, 1.0, 50.0, kBpm) == Approx(33.0));
}
