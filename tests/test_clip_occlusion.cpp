#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipOcclusion.hpp"

/**
 * Tests for what a stacked arrangement clip actually plays (#2003).
 *
 * Clips are never cut to make room for one another: the clip on top owns the
 * span it covers and the one underneath plays what is left, derived from the
 * placements every time. These are pure — no ClipManager, no engine.
 */

using namespace magda;

namespace {

ClipInfo makeClip(ClipId id, double startBeat, double lengthBeats, int stackOrder,
                  ClipType type = ClipType::Audio) {
    ClipInfo clip;
    clip.id = id;
    clip.view = ClipView::Arrangement;
    if (type == ClipType::Audio) {
        clip.setAudioContent();
        // Crossfade joints are opt-in; these tests set the flag where they mean it.
        clip.autoCrossfade = false;
    } else {
        clip.setMidiContent();
    }
    clip.setPlacementBeats(startBeat, lengthBeats);
    clip.stackOrder = stackOrder;
    return clip;
}

}  // namespace

TEST_CASE("occlusion - a clip with nothing over it plays whole", "[clip][occlusion]") {
    const std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 16.0, 8.0, 2)};

    const auto spans = computeAudibleSpans(lane);

    REQUIRE(spans.at(1).audible);
    CHECK(spans.at(1).startBeat == Catch::Approx(0.0));
    CHECK(spans.at(1).lengthBeats == Catch::Approx(8.0));
    REQUIRE(spans.at(2).audible);
    CHECK(spans.at(2).startBeat == Catch::Approx(16.0));
    CHECK(spans.at(2).lengthBeats == Catch::Approx(8.0));
}

TEST_CASE("occlusion - the clip on top takes the span it covers", "[clip][occlusion]") {
    SECTION("covering a tail shortens the clip below") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 4.0, 8.0, 2)};

        const auto spans = computeAudibleSpans(lane);

        REQUIRE(spans.at(1).audible);
        CHECK(spans.at(1).startBeat == Catch::Approx(0.0));
        CHECK(spans.at(1).lengthBeats == Catch::Approx(4.0));
        // The clip on top is untouched.
        CHECK(spans.at(2).lengthBeats == Catch::Approx(8.0));
    }

    SECTION("covering a head moves the start of the clip below") {
        const std::vector<ClipInfo> lane{makeClip(1, 8.0, 8.0, 1), makeClip(2, 4.0, 8.0, 2)};

        const auto spans = computeAudibleSpans(lane);

        REQUIRE(spans.at(1).audible);
        CHECK(spans.at(1).startBeat == Catch::Approx(12.0));
        CHECK(spans.at(1).lengthBeats == Catch::Approx(4.0));
    }

    SECTION("covering end to end silences the clip below") {
        const std::vector<ClipInfo> lane{makeClip(1, 4.0, 4.0, 1), makeClip(2, 0.0, 16.0, 2)};

        const auto spans = computeAudibleSpans(lane);

        CHECK_FALSE(spans.at(1).audible);
        CHECK(spans.at(2).audible);
    }
}

// Geometry alone cannot say who is on top: the same two placements mean
// "dropped a big clip over a small one" or "dropped a small clip onto a big
// one" depending only on which the user placed last.
TEST_CASE("occlusion - stack order decides which clip wins, not size", "[clip][occlusion]") {
    SECTION("the big clip placed last covers the small one") {
        const std::vector<ClipInfo> lane{makeClip(1, 4.0, 4.0, 1), makeClip(2, 0.0, 16.0, 2)};

        const auto spans = computeAudibleSpans(lane);

        CHECK_FALSE(spans.at(1).audible);
        CHECK(spans.at(2).lengthBeats == Catch::Approx(16.0));
    }

    SECTION("the small clip placed last sits on top of the big one") {
        const std::vector<ClipInfo> lane{makeClip(1, 4.0, 4.0, 2), makeClip(2, 0.0, 16.0, 1)};

        const auto spans = computeAudibleSpans(lane);

        CHECK(spans.at(1).audible);
        CHECK(spans.at(1).lengthBeats == Catch::Approx(4.0));
        // The clip below keeps its whole span and reports the hole in the
        // middle of it. A MIDI clip plays around that by leaving out the notes
        // inside it; an audio clip cannot, so ClipManager splits audio before
        // it ever gets here.
        CHECK(spans.at(2).audible);
        CHECK(spans.at(2).lengthBeats == Catch::Approx(16.0));
        REQUIRE(spans.at(2).silenced.size() == 1);
        CHECK(spans.at(2).silenced[0].start.value == Catch::Approx(4.0));
        CHECK(spans.at(2).silenced[0].end.value == Catch::Approx(8.0));
    }
}

TEST_CASE("occlusion - edges pull in, the middle becomes a hole", "[clip][occlusion]") {
    SECTION("a cover touching an edge shortens the clip rather than holing it") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 16.0, 1), makeClip(2, 12.0, 8.0, 2)};

        const auto spans = computeAudibleSpans(lane);

        CHECK(spans.at(1).lengthBeats == Catch::Approx(12.0));
        CHECK(spans.at(1).silenced.empty());
    }

    SECTION("two clips dropped into the middle leave two holes") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 32.0, 1), makeClip(2, 4.0, 4.0, 2),
                                         makeClip(3, 20.0, 4.0, 3)};

        const auto spans = computeAudibleSpans(lane);

        CHECK(spans.at(1).lengthBeats == Catch::Approx(32.0));
        REQUIRE(spans.at(1).silenced.size() == 2);
        CHECK(spans.at(1).silenced[0].start.value == Catch::Approx(4.0));
        CHECK(spans.at(1).silenced[1].start.value == Catch::Approx(20.0));
    }

    SECTION("abutting covers merge into one hole") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 32.0, 1), makeClip(2, 8.0, 4.0, 2),
                                         makeClip(3, 12.0, 4.0, 3)};

        const auto spans = computeAudibleSpans(lane);

        REQUIRE(spans.at(1).silenced.size() == 1);
        CHECK(spans.at(1).silenced[0].start.value == Catch::Approx(8.0));
        CHECK(spans.at(1).silenced[0].end.value == Catch::Approx(16.0));
    }
}

TEST_CASE("occlusion - a crossfade joint is not a cover", "[clip][occlusion][crossfade]") {
    std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 6.0, 8.0, 2)};
    lane[0].autoCrossfade = true;
    lane[1].autoCrossfade = true;

    const auto spans = computeAudibleSpans(lane);

    // Both keep their full span so TE can fade them into each other (#1499).
    CHECK(spans.at(1).lengthBeats == Catch::Approx(8.0));
    CHECK(spans.at(2).lengthBeats == Catch::Approx(8.0));

    SECTION("but containment still covers, flags or no flags") {
        std::vector<ClipInfo> covering{makeClip(1, 4.0, 4.0, 1), makeClip(2, 0.0, 16.0, 2)};
        covering[0].autoCrossfade = true;
        covering[1].autoCrossfade = true;

        CHECK_FALSE(computeAudibleSpans(covering).at(1).audible);
    }

    SECTION("and a MIDI overlap has no fade to protect") {
        const std::vector<ClipInfo> midi{makeClip(1, 0.0, 8.0, 1, ClipType::MIDI),
                                         makeClip(2, 6.0, 8.0, 2, ClipType::MIDI)};

        CHECK(computeAudibleSpans(midi).at(1).lengthBeats == Catch::Approx(6.0));
    }
}

// A clip switched off by hand (#1736) plays nothing, so it has nothing to
// impose on the clip below it.
TEST_CASE("occlusion - a disabled clip covers nothing", "[clip][occlusion]") {
    std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 4.0, 8.0, 2)};
    lane[1].enabled = false;

    const auto spans = computeAudibleSpans(lane);

    CHECK(spans.at(1).audible);
    CHECK(spans.at(1).lengthBeats == Catch::Approx(8.0));
}

// Stacking three deep: each clip only answers to the ones above it.
TEST_CASE("occlusion - covers accumulate down the stack", "[clip][occlusion]") {
    const std::vector<ClipInfo> lane{makeClip(1, 0.0, 16.0, 1), makeClip(2, 12.0, 8.0, 2),
                                     makeClip(3, 8.0, 8.0, 3)};

    const auto spans = computeAudibleSpans(lane);

    // 3 is on top and whole; 2 loses its head to 3; 1 loses its tail to both.
    CHECK(spans.at(3).lengthBeats == Catch::Approx(8.0));
    CHECK(spans.at(2).startBeat == Catch::Approx(16.0));
    CHECK(spans.at(2).lengthBeats == Catch::Approx(4.0));
    CHECK(spans.at(1).startBeat == Catch::Approx(0.0));
    CHECK(spans.at(1).lengthBeats == Catch::Approx(8.0));
}

// The marker in the lane comes from these ranges, so a clip that merely sits
// NEXT to another must report nothing: an adjacent clip is not a cover, and a
// duplicate lands exactly one clip-length away.
TEST_CASE("occlusion - abutting clips do not cover each other", "[clip][occlusion]") {
    SECTION("touching edges") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 8.0, 8.0, 2)};

        CHECK(computeCoveringRanges(lane, 2).empty());
        CHECK(computeCoveringRanges(lane, 1).empty());
        CHECK(computeAudibleSpans(lane).at(1).lengthBeats == Catch::Approx(8.0));
        CHECK(computeAudibleSpans(lane).at(1).silenced.empty());
    }

    SECTION("a gap between them") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 12.0, 8.0, 2)};

        CHECK(computeCoveringRanges(lane, 2).empty());
        CHECK(computeCoveringRanges(lane, 1).empty());
    }

    SECTION("a real overlap reports only the overlapping stretch") {
        const std::vector<ClipInfo> lane{makeClip(1, 0.0, 8.0, 1), makeClip(2, 6.0, 8.0, 2)};

        const auto covering = computeCoveringRanges(lane, 2);
        REQUIRE(covering.size() == 1);
        CHECK(covering[0].start.value == Catch::Approx(6.0));
        CHECK(covering[0].end.value == Catch::Approx(8.0));
    }
}
