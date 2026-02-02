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
 */

namespace {

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

    SECTION("Non-looped clip: source range spans full clip") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.audioOffset = 1.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = false;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 0.0;

        auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.sourceFileStart == Catch::Approx(1.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(5.0));  // 1.0 + 4.0 / 1.0
        REQUIRE_FALSE(di.isLooped());
    }

    SECTION("Looped clip: source range covers one loop cycle") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;  // clip is 8s long
        clip.audioOffset = 0.5;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats loop

        double bpm = 120.0;  // 4 beats = 2 seconds
        auto di = ClipDisplayInfo::from(clip, bpm);

        REQUIRE(di.loopLengthSeconds == Catch::Approx(2.0));
        REQUIRE(di.loopEndPositionSeconds == Catch::Approx(2.0));
        REQUIRE(di.isLooped());

        // Source file range for one cycle
        REQUIRE(di.sourceFileStart == Catch::Approx(0.5));  // audioOffset + 0
        REQUIRE(di.sourceFileEnd == Catch::Approx(2.5));    // 0.5 + 2.0 / 1.0
    }

    SECTION("Looped clip with stretch: source range accounts for stretch") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;  // stretched clip
        clip.audioOffset = 1.0;
        clip.audioStretchFactor = 2.0;  // 2x slower
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats

        double bpm = 120.0;  // 4 beats = 2 seconds
        auto di = ClipDisplayInfo::from(clip, bpm);

        REQUIRE(di.loopLengthSeconds == Catch::Approx(2.0));
        REQUIRE(di.isLooped());

        // Source file range: one cycle is 2s on timeline = 1s of source audio
        REQUIRE(di.sourceFileStart == Catch::Approx(1.0));  // audioOffset + 0
        REQUIRE(di.sourceFileEnd == Catch::Approx(2.0));    // 1.0 + 2.0 / 2.0
    }

    SECTION("Loop not active when loop end exceeds clip length") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 1.0;  // clip shorter than loop
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // loopEndPositionSeconds (2.0) >= length (1.0) → not looped
        REQUIRE_FALSE(di.isLooped());
    }

    SECTION("Clip shorter than loop cycle: source range clamped to clip length") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 1.0;  // 1s clip, shorter than 2s loop cycle
        clip.audioOffset = 0.5;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceFileEnd must be clamped: audioOffset + clip.length / stretch = 0.5 + 1.0 = 1.5
        // NOT the full loop cycle end of 0.5 + 2.0 = 2.5
        REQUIRE(di.sourceFileStart == Catch::Approx(0.5));
        REQUIRE(di.sourceFileEnd == Catch::Approx(1.5));
    }

    SECTION("Clip shorter than loop cycle with stretch: source range clamped") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 1.0;  // 1s on timeline
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 2.0;  // 2x slower
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // Full loop source range would be 0 + 2.0/2.0 = 1.0s of source
        // But clip is only 1.0s on timeline = 0.5s of source
        // sourceFileEnd = min(1.0, 0.0 + 1.0/2.0) = 0.5
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(0.5));
    }

    SECTION("Clip equal to loop cycle: source range not clamped") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 2.0;  // exactly one loop cycle
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // Clip length == loop cycle, no clamping needed
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(2.0));
    }

    SECTION("Clip longer than loop cycle: source range not clamped") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 6.0;  // 3x the loop cycle
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // Full loop cycle source range, no clamping
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(2.0));
        REQUIRE(di.isLooped());
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
