#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "clip/ClipSnapshot.hpp"
#include "clip/ClipStretcher.hpp"
#include "clip/EventPlacement.hpp"
#include "clip/WarpMap.hpp"
#include "exec/RenderContext.hpp"
#include "io/SourceReaders.hpp"

/**
 * A clip's own musical time, and where it stops agreeing with its file's
 * (#2038).
 *
 * All arithmetic, and tested as arithmetic: against numbers worked out by hand
 * rather than against another run of the same code. Warp adds no new machinery
 * below the position map -- the voice, the stretcher and the reading chain are
 * untouched by this slice -- so what there is to get wrong is entirely here.
 *
 * The rig's map bends the file in two directions at once, which is what makes
 * the numbers below readable. Two source seconds are squeezed into one warp
 * second, so that stretch plays at double speed; the one source second after it
 * is stretched over two, so that stretch plays at half. Everything past the last
 * marker runs at the file's own rate, which is the model's rule and the reason a
 * clip with one marker is still a clip.
 */

using magda::engine::AudioClipPlayback;
using magda::engine::AudioEventPlayback;
using magda::engine::SnapshotSpan;
using magda::engine::WarpMap;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kBpm = 120.0;
constexpr double kBeatsPerSecond = kBpm / 60.0;

Catch::Approx approx(double value, double margin = 1e-4) {
    return Catch::Approx(value).margin(margin);
}

SnapshotSpan seconds(double start, double end) {
    SnapshotSpan span;
    span.startBeat = start * kBeatsPerSecond;
    span.endBeat = end * kBeatsPerSecond;
    span.startSeconds = start;
    span.endSeconds = end;
    return span;
}

double beatAt(double seconds) {
    return seconds * kBeatsPerSecond;
}

/// Source seconds turned into the position a reading is counted in, which at
/// the rig's rates is one sample of the device per sample of the file.
double atSourceSecond(double seconds) {
    return seconds * kSampleRate;
}

/**
 * @brief The rig's map: (0,0), (2,1), (3,3).
 *
 * Source 0 to 2 over warp 0 to 1, so that stretch is consumed at twice the
 * rate; source 2 to 3 over warp 1 to 3, so the next is consumed at half. Past
 * source 3 the map carries on at slope 1.
 */
std::vector<magda::WarpMarker> markers() {
    return {{0.0, 0.0}, {2.0, 1.0}, {3.0, 3.0}};
}

/**
 * @brief A clip whose interpretation makes a warp second a timeline second.
 *
 * The event is on the beat face, as every warped event is, so its elapsed is
 * beats over its own bpm. Interpreting the file at the timeline's own tempo
 * makes those two the same number, which keeps every expectation below a fact
 * about the map rather than about a tempo conversion.
 */
AudioClipPlayback warpedClip(std::int64_t anchor = 0,
                             const std::vector<magda::WarpMarker>& warp = markers()) {
    AudioClipPlayback clip;
    clip.clipId = 1;
    clip.span = seconds(10.0, 20.0);

    AudioEventPlayback event;
    event.eventId = 1;
    event.sourceId = 1;
    event.filePath = "take.wav";
    event.sourceSampleRate = kSampleRate;
    event.sourceDurationSeconds = 10000.0;
    event.span = clip.span;
    event.anchorSamples = anchor;
    event.interpBpm = kBpm;
    event.warpEnabled = true;
    event.warp = magda::engine::compileWarpMap(warp).map;

    clip.events.push_back(std::move(event));
    return clip;
}

/// Where the clip is reading, @p elapsed timeline seconds into itself.
double readingAt(const AudioClipPlayback& clip, double elapsed) {
    const auto at = clip.span.startSeconds + elapsed;
    return magda::engine::readingPositionAt(clip, clip.events.front(), at, beatAt(at), kSampleRate);
}

}  // namespace

TEST_CASE("An empty map is the identity", "[engine][clip][warp]") {
    WarpMap map;

    REQUIRE(map.empty());
    REQUIRE(map.sourceSecondsAt(3.5) == approx(3.5));
    REQUIRE(map.sourceToWarpSeconds(3.5) == approx(3.5));
    REQUIRE(map.maxSourcePerWarp() == approx(1.0));
}

TEST_CASE("The compiled map is exact at its markers and linear between them",
          "[engine][clip][warp]") {
    const auto map = magda::engine::compileWarpMap(markers()).map;

    SECTION("at the markers") {
        REQUIRE(map.sourceSecondsAt(0.0) == approx(0.0));
        REQUIRE(map.sourceSecondsAt(1.0) == approx(2.0));
        REQUIRE(map.sourceSecondsAt(3.0) == approx(3.0));
    }

    SECTION("between them") {
        REQUIRE(map.sourceSecondsAt(0.5) == approx(1.0));
        REQUIRE(map.sourceSecondsAt(2.0) == approx(2.5));
    }

    SECTION("outside them the file runs at its own rate") {
        REQUIRE(map.sourceSecondsAt(-1.0) == approx(-1.0));
        REQUIRE(map.sourceSecondsAt(5.0) == approx(5.0));
    }

    SECTION("the forward direction is the model's own") {
        // AudioEvent::warpedSourceSeconds, which the editors read.
        REQUIRE(map.sourceToWarpSeconds(1.0) == approx(0.5));
        REQUIRE(map.sourceToWarpSeconds(2.5) == approx(2.0));
        REQUIRE(map.sourceToWarpSeconds(9.0) == approx(9.0));
    }

    SECTION("one composed with the other is the identity") {
        for (const auto at : {0.25, 1.5, 2.75, 4.0})
            REQUIRE(map.sourceSecondsAt(map.sourceToWarpSeconds(at)) == approx(at));
    }

    SECTION("the steepest segment is what a stretcher is sized against") {
        REQUIRE(map.maxSourcePerWarp() == approx(2.0));
    }
}

TEST_CASE("Compiling refuses what it cannot invert", "[engine][clip][warp]") {
    SECTION("markers arrive in whatever order they were stored in") {
        const auto shuffled = magda::engine::compileWarpMap({{3.0, 3.0}, {0.0, 0.0}, {2.0, 1.0}});

        REQUIRE(shuffled.droppedMarkers == 0);
        REQUIRE(shuffled.map.points.size() == 3);
        REQUIRE(shuffled.map.sourceSecondsAt(1.0) == approx(2.0));
    }

    SECTION("a marker dragged back past its neighbour is dropped, not obeyed") {
        // Source climbs, warp does not: there is no source second that plays at
        // warp 2, and asking would divide by a negative span.
        const auto backwards = magda::engine::compileWarpMap({{0.0, 0.0}, {2.0, 3.0}, {3.0, 2.0}});

        REQUIRE(backwards.droppedMarkers == 1);
        REQUIRE(backwards.map.points.size() == 2);
    }

    SECTION("one marker flung forwards costs itself and not the rest") {
        // A greedy pass would anchor on the marker at warp 100 and drop the
        // three behind it. What should go is the one marker that is wrong.
        const auto flung = magda::engine::compileWarpMap(
            {{0.0, 0.0}, {1.0, 100.0}, {2.0, 1.0}, {3.0, 2.0}, {4.0, 3.0}});

        REQUIRE(flung.droppedMarkers == 1);
        REQUIRE(flung.map.points.size() == 4);
        REQUIRE(flung.map.points[1].warpSeconds == approx(1.0));
        REQUIRE(flung.map.points.back().warpSeconds == approx(3.0));
    }

    SECTION("compiling one list twice keeps the same markers") {
        const std::vector<magda::WarpMarker> awkward{
            {0.0, 0.0}, {1.0, 100.0}, {2.0, 1.0}, {3.0, 2.0}, {4.0, 3.0}};

        REQUIRE(magda::engine::compileWarpMap(awkward).map ==
                magda::engine::compileWarpMap(awkward).map);
    }

    SECTION("two markers at one instant collapse") {
        const auto coincident = magda::engine::compileWarpMap({{0.0, 0.0}, {2.0, 1.0}, {2.0, 1.5}});

        REQUIRE(coincident.droppedMarkers == 1);
        REQUIRE(coincident.map.points.size() == 2);
    }

    SECTION("what is not a number never reaches the audio thread") {
        const auto broken =
            magda::engine::compileWarpMap({{0.0, 0.0}, {std::nan(""), 1.0}, {3.0, std::nan("")}});

        REQUIRE(broken.droppedMarkers == 2);
        REQUIRE(broken.map.points.size() == 1);
    }
}

TEST_CASE("A warped clip consumes its file at the rate its markers ask for",
          "[engine][clip][warp]") {
    const auto clip = warpedClip();

    SECTION("the squeezed stretch plays at double speed") {
        REQUIRE(readingAt(clip, 0.0) == approx(atSourceSecond(0.0)));
        REQUIRE(readingAt(clip, 0.5) == approx(atSourceSecond(1.0)));
        REQUIRE(readingAt(clip, 1.0) == approx(atSourceSecond(2.0)));
    }

    SECTION("the stretched one plays at half") {
        REQUIRE(readingAt(clip, 2.0) == approx(atSourceSecond(2.5)));
        REQUIRE(readingAt(clip, 3.0) == approx(atSourceSecond(3.0)));
    }

    SECTION("past the last marker it plays at the file's own rate") {
        REQUIRE(readingAt(clip, 4.0) == approx(atSourceSecond(4.0)));
        REQUIRE(readingAt(clip, 5.0) == approx(atSourceSecond(5.0)));
    }

    SECTION("the position only ever moves forwards") {
        auto last = readingAt(clip, 0.0);

        for (double at = 0.05; at < 9.0; at += 0.05) {
            const auto now = readingAt(clip, at);
            REQUIRE(now > last);
            last = now;
        }
    }
}

TEST_CASE("Warp sizes a stretcher against its steepest stretch, not its average",
          "[engine][clip][warp]") {
    const auto clip = warpedClip();

    REQUIRE(magda::engine::readingRateOf(clip.events.front()) == approx(2.0));
}

TEST_CASE("Trimming a clip's head leaves its markers pinned to the file", "[engine][clip][warp]") {
    // Reading begins one source second in, which is halfway through the
    // squeezed stretch: warp 0.5. What plays from there is what played from
    // there before the trim, rather than the map sliding with the anchor.
    const auto clip = warpedClip(static_cast<std::int64_t>(atSourceSecond(1.0)));

    REQUIRE(readingAt(clip, 0.0) == approx(atSourceSecond(1.0)));
    REQUIRE(readingAt(clip, 0.5) == approx(atSourceSecond(2.0)));

    // Warp 2.0, which is halfway through the stretched segment rather than
    // halfway through the second the trim removed.
    REQUIRE(readingAt(clip, 1.5) == approx(atSourceSecond(2.5)));
}

TEST_CASE("Where the pool cues a warped clip is where its first block reads",
          "[engine][clip][warp]") {
    for (const auto anchor : {0.0, 1.0, 2.5}) {
        const auto clip = warpedClip(static_cast<std::int64_t>(atSourceSecond(anchor)));
        const auto placement = magda::engine::placementFor(clip.events.front(), kSampleRate);

        REQUIRE(static_cast<double>(placement.sourceOffsetSamples) ==
                approx(readingAt(clip, 0.0), 1.0));
    }
}

TEST_CASE("A reversed warped clip keeps its markers", "[engine][clip][warp]") {
    // The incumbent cannot do this at all: it bakes warp into a proxy file and
    // returns the reverse job before it reaches the warp one, so a reversed
    // warped clip there plays unwarped.
    auto clip = warpedClip();
    auto& event = clip.events.front();
    event.reversed = true;

    // The mirrored file's own coordinates, which is what the reading delivers.
    const auto mirrored = [](double sourceSeconds) {
        return atSourceSecond(10000.0) - 1.0 - atSourceSecond(sourceSeconds);
    };

    SECTION("the first thing heard is the far end of what it reads") {
        // Ten warp seconds of event, and past marker three the map runs at
        // slope 1, so its far end is source second ten.
        REQUIRE(readingAt(clip, 0.0) == approx(mirrored(10.0)));
    }

    SECTION("the bend is heard in the opposite order") {
        REQUIRE(readingAt(clip, 7.0) == approx(mirrored(3.0)));
        REQUIRE(readingAt(clip, 8.0) == approx(mirrored(2.5)));
        REQUIRE(readingAt(clip, 9.0) == approx(mirrored(2.0)));
        REQUIRE(readingAt(clip, 9.5) == approx(mirrored(1.0)));
    }

    SECTION("the reading still only moves forwards") {
        auto last = readingAt(clip, 0.0);

        for (double at = 0.05; at < 9.9; at += 0.05) {
            const auto now = readingAt(clip, at);
            REQUIRE(now > last);
            last = now;
        }
    }
}

TEST_CASE("A warped loop bends the same way on every pass", "[engine][clip][warp]") {
    auto clip = warpedClip();
    auto& event = clip.events.front();
    event.loopEnabled = true;
    event.loopStartSamples = 0;
    event.loopLengthSamples = static_cast<std::int64_t>(atSourceSecond(2.0));

    // Source 0 to 2 is warp 0 to 1, so the loop is one warp second long.
    SECTION("the second pass reads what the first did") {
        REQUIRE(readingAt(clip, 1.5) == approx(readingAt(clip, 0.5)));
        REQUIRE(readingAt(clip, 2.25) == approx(readingAt(clip, 0.25)));
        REQUIRE(readingAt(clip, 5.75) == approx(readingAt(clip, 0.75)));
    }

    SECTION("and it is still the warped reading, not a straight one") {
        REQUIRE(readingAt(clip, 0.25) == approx(atSourceSecond(0.5)));
        REQUIRE(readingAt(clip, 1.25) == approx(atSourceSecond(0.5)));
    }

    SECTION("the reading chain does not tile it a second time") {
        // Folding happens in warp time, above the map. Tiling below the stream
        // as well would fold a position that had already been folded.
        const auto how = magda::engine::sourceReadFor(event, kSampleRate);
        REQUIRE(how.loopLengthSamples == 0);
    }
}

TEST_CASE("Warp with no markers is still warp", "[engine][clip][warp]") {
    // Identity as a map, and an interpretation as far as the model is
    // concerned: the event stays on the beat face at its own bpm and still
    // wants a stretcher. Deleting the last marker off a clip must not quietly
    // stop it following the tempo.
    auto clip = warpedClip(0, {});
    auto& event = clip.events.front();
    event.interpBpm = kBpm / 2.0;  // half the timeline's, so twice as fast

    REQUIRE(event.warp.empty());
    REQUIRE(event.warpEnabled);
    REQUIRE(magda::engine::readingRateOf(event) == approx(2.0));
    REQUIRE(readingAt(clip, 1.0) == approx(atSourceSecond(2.0)));

    SECTION("and it still asks for a stretcher that can follow") {
        const magda::engine::RenderContext context{kSampleRate, 512, 2};
        REQUIRE(magda::engine::stretchSetupFor(clip, event, context).followsTempo);
    }
}

TEST_CASE("A warp second is not a timeline second when the file disagrees",
          "[engine][clip][warp]") {
    // The rig above interprets the file at the timeline's own tempo, which
    // makes the beat-face conversion a multiply by one and hides a units slip.
    // Here the file is read at half the timeline's tempo, so a timeline second
    // is two warp seconds and the map is walked twice as fast.
    auto clip = warpedClip();
    auto& event = clip.events.front();
    event.interpBpm = kBpm / 2.0;

    // Half a second of timeline is one warp second, which is source second two.
    REQUIRE(readingAt(clip, 0.5) == approx(atSourceSecond(2.0)));

    // And one and a half is warp three, the last marker.
    REQUIRE(readingAt(clip, 1.5) == approx(atSourceSecond(3.0)));
}

TEST_CASE("A reversed warped loop composes both", "[engine][clip][warp]") {
    auto clip = warpedClip();
    auto& event = clip.events.front();
    event.reversed = true;
    event.loopEnabled = true;
    event.loopStartSamples = 0;
    event.loopLengthSamples = static_cast<std::int64_t>(atSourceSecond(2.0));

    SECTION("it repeats on the loop's warped length") {
        REQUIRE(readingAt(clip, 1.5) == approx(readingAt(clip, 0.5)));
        REQUIRE(readingAt(clip, 3.25) == approx(readingAt(clip, 0.25)));
    }

    SECTION("and every position is still in the mirrored file") {
        const auto mirrored = [](double sourceSeconds) {
            return atSourceSecond(10000.0) - 1.0 - atSourceSecond(sourceSeconds);
        };

        // Warp folds into [0, 1); walking backwards from the far end lands at
        // warp 0 exactly, which is source zero.
        REQUIRE(readingAt(clip, 0.0) == approx(mirrored(0.0)));
    }

    SECTION("the reading chain still does not tile it a second time") {
        REQUIRE(magda::engine::sourceReadFor(event, kSampleRate).loopLengthSamples == 0);
    }
}

TEST_CASE("An unwarped event is untouched by any of this", "[engine][clip][warp]") {
    auto clip = warpedClip(500, {});
    auto& event = clip.events.front();
    event.warpEnabled = false;

    REQUIRE(event.warp.empty());
    REQUIRE(magda::engine::readingPositionAt(clip, event, 11.0, beatAt(11.0), kSampleRate) ==
            approx(500.0 + kSampleRate));

    SECTION("including one that loops, which still tiles below the stream") {
        event.loopEnabled = true;
        event.loopStartSamples = 0;
        event.loopLengthSamples = static_cast<std::int64_t>(atSourceSecond(2.0));

        const auto how = magda::engine::sourceReadFor(event, kSampleRate);
        REQUIRE(how.loopLengthSamples == static_cast<std::int64_t>(atSourceSecond(2.0)));
    }
}
