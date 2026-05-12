#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipDisplayInfo.hpp"
#include "magda/daw/core/ClipInfo.hpp"

/**
 * Tests for the looped waveform partial-tile source range fix.
 *
 * Bug: when a looped clip's length was not an exact multiple of the loop
 * cycle, the last (partial) tile drew the full loop cycle's source audio
 * range into a shorter pixel rect, visually compressing/stretching the
 * waveform.
 *
 * Fix: partial tiles scale the source range proportionally, i.e.
 *   fraction = tileDuration / loopCycle
 *   tileFileEnd = fileStart + (fileEnd - fileStart) * fraction
 *
 * TE-aligned Model:
 * - offset: start position in source file (seconds)
 * - loopStart/loopLength: loop region in source file (seconds)
 * - speedRatio: playback speed ratio (1.0 = normal, 2.0 = 2x faster)
 * - loopEnabled: whether to loop
 * - offset: start position in source file, also encodes loop phase (seconds)
 */

namespace {

void syncPlacement(magda::ClipInfo& clip, double bpm = 120.0) {
    clip.setPlacementBeats(clip.startTime * bpm / 60.0, clip.length * bpm / 60.0);
}

/// Mirrors the tile source-range calculation used in ClipComponent::paintAudioClip
/// and WaveformGridComponent::paintWaveformThumbnail for looped clips.
struct TileSourceRange {
    double fileStart;
    double fileEnd;
};

TileSourceRange computeTileSourceRange(double timePos, double loopCycle, double clipLength,
                                       double sourceFileStart, double sourceFileEnd) {
    double cycleEnd = std::min(timePos + loopCycle, clipLength);
    double tileDuration = cycleEnd - timePos;
    double tileFileEnd = sourceFileEnd;

    constexpr double kEpsilon = 0.0001;
    if (tileDuration < loopCycle - kEpsilon) {
        double fraction = tileDuration / loopCycle;
        tileFileEnd = sourceFileStart + (sourceFileEnd - sourceFileStart) * fraction;
    }

    return {sourceFileStart, tileFileEnd};
}

}  // namespace

// ============================================================================
// ClipDisplayInfo loop parameter tests
// ============================================================================

TEST_CASE("ClipDisplayInfo - looped source file ranges", "[clip][display][loop]") {
    using namespace magda;

    // Under the new contract these tests pin two invariants per scenario:
    //
    //   1. Loop region fields  (loopRegionStartSource / loopRegionLengthSource,
    //      loopStartPositionSeconds / loopEndPositionSeconds) reflect the
    //      user's loop selection.
    //   2. File extent fields  (sourceFileStart / sourceFileEnd) describe
    //      the source file on disk, INDEPENDENTLY of the loop region.
    //
    // The previous tests asserted "sourceFileEnd == loopStart + loopLength"
    // which was the conflation that caused the editor to truncate the
    // waveform whenever the loop region was smaller than the file. Each
    // section below now exercises both fields side-by-side.

    SECTION("Non-looped clip: file extent uses the file (or a fallback derived from clip length)") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 1.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;
        clip.loopStart = 0.0;
        clip.loopLength = 0.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0);  // fileDuration=0 → fallback

        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd > 0.0);
        REQUIRE_FALSE(di.isLooped());
    }

    SECTION("Looped clip: loop region tracks the selection; file extent stays full file") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 0.5;
        clip.loopLength = 2.0;
        clip.offset = clip.loopStart;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        // Loop region matches selection.
        REQUIRE(di.loopRegionStartSource == Catch::Approx(0.5));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(2.0));
        REQUIRE(di.loopStartPositionSeconds == Catch::Approx(0.5));
        REQUIRE(di.loopEndPositionSeconds == Catch::Approx(2.5));
        REQUIRE(di.loopLengthSeconds == Catch::Approx(2.0));
        REQUIRE(di.isLooped());

        // File extent = full file, regardless of loop.
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(4.0));
    }

    SECTION(
        "Looped clip with stretch: loop in timeline scales by speedRatio; file extent unchanged") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.speedRatio = 2.0;  // 2x faster
        clip.loopEnabled = true;
        clip.loopStart = 1.0;
        clip.loopLength = 1.0;
        clip.offset = clip.loopStart;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/3.0);

        REQUIRE(di.loopLengthSeconds == Catch::Approx(0.5));  // 1s source / 2x = 0.5s timeline
        REQUIRE(di.loopRegionStartSource == Catch::Approx(1.0));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(1.0));
        REQUIRE(di.isLooped());

        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(3.0));
        REQUIRE(di.fileExtentTimeline() == Catch::Approx(1.5));  // 3s / 2x
    }

    SECTION("loopEnabled with loopLength=0: sentinel falls back to remaining file") {
        // Older clips and freshly-toggled loops can land in this state.
        // The session scheduler treats it as "loop the whole source from
        // loopStart" so the editor must too — otherwise the clip plays
        // looped while the overlay draws it as non-looped.
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 1.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 0.0;
        clip.loopLength = 0.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/1.0);

        REQUIRE(di.isLooped());
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(1.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(1.0));
    }

    SECTION("Loop region inside a longer file: loop fields == selection, file extent == file") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 1.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 0.5;
        clip.loopLength = 2.0;
        clip.offset = clip.loopStart;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.loopRegionStartSource == Catch::Approx(0.5));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(2.0));
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(4.0));
    }

    SECTION("Clip = one loop cycle: loop region matches; file extent untouched") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 2.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 0.0;
        clip.loopLength = 2.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/2.0);

        REQUIRE(di.loopRegionStartSource == Catch::Approx(0.0));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(2.0));
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(2.0));
    }

    SECTION("Clip > loop cycle: loop region unchanged, file extent shows full file") {
        // The bug repro: file is longer than the loop region. Pre-fix this
        // would have given sourceFileEnd == loopLength (= 2.0), shrinking
        // the editor's waveform window. Post-fix the file extent is the
        // full file even though the loop is shorter.
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 6.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 0.0;
        clip.loopLength = 2.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.loopRegionLengthSource == Catch::Approx(2.0));
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(4.0));  // FULL file, not loop
        REQUIRE(di.isLooped());
    }
}

TEST_CASE("ClipDisplayInfo maps session playhead into waveform editor display time",
          "[clip][display][loop][session-playhead]") {
    using namespace magda;

    SECTION("Non-looped clips start from the source offset display position") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.offset = 1.5;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;

        syncPlacement(clip);
        const auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.sessionPlayheadToDisplayPosition(-0.001) == Catch::Approx(-1.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(0.0) == Catch::Approx(1.5));
        REQUIRE(di.sessionPlayheadToDisplayPosition(2.25) == Catch::Approx(3.75));
    }

    SECTION("Looped clips at zero phase wrap inside the loop display range") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 12.0;
        clip.offset = 1.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 1.0;
        clip.loopLength = 4.0;

        syncPlacement(clip);
        const auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.sessionPlayheadToDisplayPosition(0.0) == Catch::Approx(1.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(1.5) == Catch::Approx(2.5));
        REQUIRE(di.sessionPlayheadToDisplayPosition(4.0) == Catch::Approx(1.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(5.25) == Catch::Approx(2.25));
    }

    SECTION("Looped clips preserve phase offset when wrapping") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 12.0;
        clip.offset = 2.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 1.0;
        clip.loopLength = 4.0;

        syncPlacement(clip);
        const auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.sessionPlayheadToDisplayPosition(0.0) == Catch::Approx(2.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(2.5) == Catch::Approx(4.5));
        REQUIRE(di.sessionPlayheadToDisplayPosition(3.0) == Catch::Approx(1.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(4.5) == Catch::Approx(2.5));
    }

    SECTION("Auto-tempo source BPM maps session playhead in display-time seconds") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.autoTempo = true;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.audio().interpretation.bpm = 172.0;
        clip.audio().interpretation.totalBeats = 16.0;
        clip.audio().source.durationSeconds = 16.0 * 60.0 / 172.0;
        clip.setPlacementBeats(0.0, 16.0);
        clip.loopStartBeats = 0.0;
        clip.loopLengthBeats = 16.0;
        clip.offsetBeats = 4.0;

        const auto di = ClipDisplayInfo::from(clip, 120.0, clip.audio().source.durationSeconds);

        REQUIRE(di.loopLengthSeconds == Catch::Approx(8.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(0.0) == Catch::Approx(2.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(1.0) == Catch::Approx(3.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(6.0) == Catch::Approx(0.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(8.5) == Catch::Approx(2.5));
    }

    SECTION("Auto-tempo playhead mapping ignores stale source seconds caches") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.autoTempo = true;
        clip.startTime = 99.0;  // stale cache; placement is authoritative
        clip.length = 99.0;     // stale cache; placement is authoritative
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.audio().interpretation.bpm = 172.0;
        clip.audio().interpretation.totalBeats = 16.0;
        clip.audio().source.durationSeconds = 16.0 * 60.0 / 172.0;
        clip.setPlacementBeats(0.0, 16.0);
        clip.loopStart = 99.0;   // stale cache; loopStartBeats is authoritative
        clip.loopLength = 99.0;  // stale cache; loopLengthBeats is authoritative
        clip.offset = 99.0;      // stale cache; offsetBeats is authoritative
        clip.loopStartBeats = 4.0;
        clip.loopLengthBeats = 8.0;
        clip.offsetBeats = 6.0;

        const auto di = ClipDisplayInfo::from(clip, 120.0, clip.audio().source.durationSeconds);

        REQUIRE(di.loopStartPositionSeconds == Catch::Approx(2.0));
        REQUIRE(di.loopLengthSeconds == Catch::Approx(4.0));
        REQUIRE(di.offsetPositionSeconds == Catch::Approx(3.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(0.0) == Catch::Approx(3.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(3.0) == Catch::Approx(2.0));
        REQUIRE(di.sessionPlayheadToDisplayPosition(5.5) == Catch::Approx(4.5));
    }

    SECTION("Auto-tempo display source conversion follows source BPM after Beats edit") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.autoTempo = true;
        clip.loopEnabled = true;
        clip.speedRatio = 1.0;
        clip.audio().interpretation.bpm = 129.0;
        clip.audio().interpretation.totalBeats = 12.0;
        clip.audio().source.durationSeconds = 12.0 * 60.0 / 129.0;
        clip.setPlacementBeats(0.0, 12.0);
        clip.loopStartBeats = 0.0;
        clip.loopLengthBeats = 12.0;
        clip.offsetBeats = 3.0;
        clip.loopStart = 99.0;
        clip.loopLength = 99.0;
        clip.offset = 99.0;

        const auto di = ClipDisplayInfo::from(clip, 120.0, clip.audio().source.durationSeconds);

        REQUIRE(di.fileExtentTimeline() == Catch::Approx(6.0));
        REQUIRE(di.loopLengthSeconds == Catch::Approx(6.0));
        REQUIRE(di.offsetPositionSeconds == Catch::Approx(1.5));
        REQUIRE(di.displayPositionToSourceTime(1.5) == Catch::Approx(1.5 * 120.0 / 129.0));
    }
}

// ============================================================================
// Partial tile source range calculation
// ============================================================================

TEST_CASE("Looped waveform tile - full tiles use full source range",
          "[clip][waveform][loop][tile]") {
    // Loop cycle = 2.0s, source range = [1.0, 3.0] (2s of source audio)
    double loopCycle = 2.0;
    double sourceStart = 1.0;
    double sourceEnd = 3.0;
    double clipLength = 8.0;  // exactly 4 full cycles

    SECTION("First tile") {
        auto range = computeTileSourceRange(0.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileStart == Catch::Approx(1.0));
        REQUIRE(range.fileEnd == Catch::Approx(3.0));
    }

    SECTION("Second tile") {
        auto range = computeTileSourceRange(2.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileStart == Catch::Approx(1.0));
        REQUIRE(range.fileEnd == Catch::Approx(3.0));
    }

    SECTION("Last full tile") {
        auto range = computeTileSourceRange(6.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileStart == Catch::Approx(1.0));
        REQUIRE(range.fileEnd == Catch::Approx(3.0));
    }
}

TEST_CASE("Looped waveform tile - partial tiles reduce source range proportionally",
          "[clip][waveform][loop][tile][regression]") {
    // Loop cycle = 2.0s, source range = [1.0, 3.0] (2s of source audio)
    double loopCycle = 2.0;
    double sourceStart = 1.0;
    double sourceEnd = 3.0;
    double sourceRange = sourceEnd - sourceStart;  // 2.0

    SECTION("50% partial tile") {
        double clipLength = 5.0;  // 2 full + 1s partial
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);

        // tileDuration = min(4+2, 5) - 4 = 1.0 → 50% of cycle
        REQUIRE(range.fileStart == Catch::Approx(sourceStart));
        REQUIRE(range.fileEnd == Catch::Approx(sourceStart + sourceRange * 0.5));  // 2.0
    }

    SECTION("25% partial tile") {
        double clipLength = 4.5;  // 2 full + 0.5s partial
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);

        // tileDuration = min(4+2, 4.5) - 4 = 0.5 → 25% of cycle
        REQUIRE(range.fileStart == Catch::Approx(sourceStart));
        REQUIRE(range.fileEnd == Catch::Approx(sourceStart + sourceRange * 0.25));  // 1.5
    }

    SECTION("75% partial tile") {
        double clipLength = 5.5;  // 2 full + 1.5s partial
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);

        // tileDuration = min(4+2, 5.5) - 4 = 1.5 → 75% of cycle
        REQUIRE(range.fileStart == Catch::Approx(sourceStart));
        REQUIRE(range.fileEnd == Catch::Approx(sourceStart + sourceRange * 0.75));  // 2.5
    }

    SECTION("Very small partial tile (5%)") {
        double clipLength = 4.1;  // 2 full + 0.1s partial
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);

        // tileDuration = 0.1 → 5% of cycle
        REQUIRE(range.fileStart == Catch::Approx(sourceStart));
        REQUIRE(range.fileEnd == Catch::Approx(sourceStart + sourceRange * 0.05));
    }

    SECTION("Nearly full tile (99%)") {
        double clipLength = 5.98;  // 2 full + 1.98s partial
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);

        // tileDuration = 1.98 → 99% of cycle
        REQUIRE(range.fileStart == Catch::Approx(sourceStart));
        REQUIRE(range.fileEnd == Catch::Approx(sourceStart + sourceRange * 0.99));
    }
}

TEST_CASE("Looped waveform tile - partial tile with stretch factor",
          "[clip][waveform][loop][tile][stretch]") {
    // Stretched 2x: loop cycle = 2s on timeline, source audio = 1s
    double loopCycle = 2.0;
    double sourceStart = 0.5;
    double sourceEnd = 1.5;  // 1.0s of source (= 2.0s / 2.0 stretch)

    SECTION("Full tile with stretch") {
        double clipLength = 6.0;
        auto range = computeTileSourceRange(0.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileStart == Catch::Approx(0.5));
        REQUIRE(range.fileEnd == Catch::Approx(1.5));
    }

    SECTION("50% partial tile with stretch") {
        double clipLength = 5.0;  // last tile: 5-4=1s of timeline = 50%
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);

        // 50% of source range (1.0) = 0.5
        REQUIRE(range.fileStart == Catch::Approx(0.5));
        REQUIRE(range.fileEnd == Catch::Approx(1.0));  // 0.5 + 0.5
    }
}

TEST_CASE("Looped waveform tile - exact clip length multiples need no adjustment",
          "[clip][waveform][loop][tile]") {
    double loopCycle = 2.0;
    double sourceStart = 0.0;
    double sourceEnd = 2.0;

    SECTION("Clip length = exactly 1 cycle") {
        double clipLength = 2.0;
        auto range = computeTileSourceRange(0.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileEnd == Catch::Approx(sourceEnd));
    }

    SECTION("Clip length = exactly 3 cycles") {
        double clipLength = 6.0;
        // Check last tile (starts at 4.0)
        auto range = computeTileSourceRange(4.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileEnd == Catch::Approx(sourceEnd));
    }

    SECTION("Clip length = exactly 10 cycles") {
        double clipLength = 20.0;
        // Check last tile (starts at 18.0)
        auto range = computeTileSourceRange(18.0, loopCycle, clipLength, sourceStart, sourceEnd);
        REQUIRE(range.fileEnd == Catch::Approx(sourceEnd));
    }
}

TEST_CASE("Looped waveform tile - full tile iteration produces correct ranges",
          "[clip][waveform][loop][tile][integration]") {
    // Simulate the full tiling loop as done in paintAudioClip
    double loopCycle = 2.0;
    double sourceStart = 1.0;
    double sourceEnd = 3.0;
    double clipLength = 7.0;  // 3 full cycles + 1s partial

    int fullTileCount = 0;
    int partialTileCount = 0;
    double timePos = 0.0;

    while (timePos < clipLength) {
        auto range = computeTileSourceRange(timePos, loopCycle, clipLength, sourceStart, sourceEnd);

        double cycleEnd = std::min(timePos + loopCycle, clipLength);
        double tileDuration = cycleEnd - timePos;

        if (tileDuration >= loopCycle - 0.0001) {
            // Full tile: source range must equal full range
            fullTileCount++;
            REQUIRE(range.fileEnd == Catch::Approx(sourceEnd));
        } else {
            // Partial tile: source range must be proportional
            partialTileCount++;
            double expectedFraction = tileDuration / loopCycle;
            double expectedEnd = sourceStart + (sourceEnd - sourceStart) * expectedFraction;
            REQUIRE(range.fileEnd == Catch::Approx(expectedEnd));
        }

        // Pixel width proportional to source range (no stretch)
        double pixelsPerSecond = 100.0;  // arbitrary zoom
        double drawWidth = tileDuration * pixelsPerSecond;
        double sourceRangeDrawn = range.fileEnd - range.fileStart;

        // Key invariant: pixels per source-second must be constant across all tiles.
        // This is what prevents visual stretching.
        if (sourceRangeDrawn > 0.001) {
            double pxPerSourceSec = drawWidth / sourceRangeDrawn;
            // With stretch=1.0, should equal pixelsPerSecond
            REQUIRE(pxPerSourceSec == Catch::Approx(pixelsPerSecond));
        }

        timePos += loopCycle;
    }

    REQUIRE(fullTileCount == 3);
    REQUIRE(partialTileCount == 1);
}
