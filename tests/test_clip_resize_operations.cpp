#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for ClipOperations resize methods
 *
 * These tests verify:
 * - resizeContainerFromLeft compensates source.position so audio stays at
 *   the same absolute timeline position
 * - resizeContainerFromRight only changes clip.length, source unchanged
 * - Sequential resize operations maintain correct state
 * - Visible region and file time calculation (time-domain waveform rendering)
 *
 * Bug fixed: resizeContainerFromLeft did not adjust source.position when
 * clip.startTime moved. Since source.position is relative to clip.startTime,
 * the audio appeared to shift on the timeline after resize.
 */

using namespace magda;

// ============================================================================
// resizeContainerFromLeft - source position compensation
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromLeft - trims audio source", "[clip][resize][left]") {
    SECTION("Shrinking from left trims source (audio at clip start)") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 0.0;
        source.offset = 0.0;
        source.length = 4.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Shrink from left to 3.0 seconds (clip moves right by 1.0)
        ClipOperations::resizeContainerFromLeft(clip, 3.0);

        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.length == 3.0);

        // Source trimmed: offset advances, length shrinks, position stays at 0.0
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(1.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(3.0));
        REQUIRE(clip.audioSources[0].position == Catch::Approx(0.0));
    }

    SECTION("Shrinking from left with stretch factor converts trim to file time") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 0.0;
        source.offset = 0.0;
        source.length = 8.0;
        source.stretchFactor = 2.0;  // 2x slower
        clip.audioSources.push_back(source);

        // Shrink from left by 2.0 timeline seconds
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);

        // File offset advances by 2.0 / 2.0 = 1.0 file seconds
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(1.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(6.0));
        REQUIRE(clip.audioSources[0].position == Catch::Approx(0.0));
        REQUIRE(clip.audioSources[0].stretchFactor == 2.0);  // Unchanged
    }

    SECTION("Expanding from left does not trim (reveals empty space)") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 0.0;
        source.offset = 0.0;
        source.length = 4.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Expand from left to 6.0 seconds (clip moves left by 2.0)
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 6.0);

        // Source not trimmed (position goes positive, no negative to trim)
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);
        REQUIRE(clip.audioSources[0].position == Catch::Approx(2.0));
    }

    SECTION("Shrink only into empty space before audio - no source trimming") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 3.0;  // Audio starts 3s into clip
        source.offset = 0.0;
        source.length = 4.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Shrink from left by 2.0 (only removes empty space before audio)
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);

        // No trimming needed — audio position adjusted but still >= 0
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);
        REQUIRE(clip.audioSources[0].position == Catch::Approx(1.0));  // 3.0 - 2.0
    }

    SECTION("Shrink partially into audio block") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 1.0;  // Audio starts 1s into clip
        source.offset = 0.0;
        source.length = 5.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Shrink from left by 2.0 (removes 1.0 empty + 1.0 of audio)
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);

        // Audio trimmed by 1.0 second (the part that went negative)
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(1.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(4.0));  // 5.0 - 1.0
        REQUIRE(clip.audioSources[0].position == Catch::Approx(0.0));
    }

    SECTION("Multiple sources: one trimmed, one just repositioned") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 10.0;
        clip.type = ClipType::Audio;

        AudioSource src1;
        src1.filePath = "test1.wav";
        src1.position = 0.0;
        src1.offset = 0.0;
        src1.length = 4.0;
        src1.stretchFactor = 1.0;

        AudioSource src2;
        src2.filePath = "test2.wav";
        src2.position = 5.0;
        src2.offset = 0.0;
        src2.length = 3.0;
        src2.stretchFactor = 1.0;

        clip.audioSources.push_back(src1);
        clip.audioSources.push_back(src2);

        // Shrink from left by 1.0
        ClipOperations::resizeContainerFromLeft(clip, 9.0);

        REQUIRE(clip.startTime == 1.0);

        // Source 1: was at position 0.0, now -1.0 → trimmed
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(1.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(3.0));
        REQUIRE(clip.audioSources[0].position == Catch::Approx(0.0));

        // Source 2: was at position 5.0, now 4.0 → no trim needed
        REQUIRE(clip.audioSources[1].offset == 0.0);
        REQUIRE(clip.audioSources[1].length == 3.0);
        REQUIRE(clip.audioSources[1].position == Catch::Approx(4.0));
    }

    SECTION("Expand past zero clamps startTime correctly") {
        ClipInfo clip;
        clip.startTime = 1.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 0.0;
        source.offset = 0.0;
        source.length = 4.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Try to expand to 8.0 (would put startTime at -3.0, clamped to 0.0)
        ClipOperations::resizeContainerFromLeft(clip, 8.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 8.0);

        // Source position moves positive (expanding), no trim
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);
        REQUIRE(clip.audioSources[0].position == Catch::Approx(1.0));
    }
}

// ============================================================================
// resizeContainerFromRight - source unchanged
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromRight - source data unchanged",
          "[clip][resize][right]") {
    SECTION("Shrinking from right does not modify source") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 0.5;
        source.offset = 1.0;
        source.length = 3.0;
        source.stretchFactor = 1.5;
        clip.audioSources.push_back(source);

        ClipOperations::resizeContainerFromRight(clip, 3.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 3.0);

        // All source properties unchanged
        REQUIRE(clip.audioSources[0].position == 0.5);
        REQUIRE(clip.audioSources[0].offset == 1.0);
        REQUIRE(clip.audioSources[0].length == 3.0);
        REQUIRE(clip.audioSources[0].stretchFactor == 1.5);
    }

    SECTION("Expanding from right does not modify source") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 0.0;
        source.offset = 0.0;
        source.length = 4.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        ClipOperations::resizeContainerFromRight(clip, 8.0);

        REQUIRE(clip.startTime == 2.0);  // Unchanged
        REQUIRE(clip.length == 8.0);

        REQUIRE(clip.audioSources[0].position == 0.0);
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);
        REQUIRE(clip.audioSources[0].stretchFactor == 1.0);
    }

    SECTION("Minimum length enforced") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;

        ClipOperations::resizeContainerFromRight(clip, 0.01);
        REQUIRE(clip.length == Catch::Approx(ClipOperations::MIN_CLIP_LENGTH));
    }
}

// ============================================================================
// Sequential resize operations
// ============================================================================

TEST_CASE("ClipOperations - Sequential resizes maintain absolute audio position",
          "[clip][resize][sequential][regression]") {
    /**
     * REGRESSION TEST
     *
     * Bug scenario from user report:
     * "2 bar loop with 8 kick hits, 1 per beat. I remove 1 beat and I expect
     *  to see 7 kicks, but I see 6 kicks in the same space."
     *
     * The issue was that source.position wasn't compensated during left resize,
     * causing the waveform rendering to use wrong source boundaries.
     */
    SECTION("Multiple left resizes trim source progressively") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;  // 2 bars at 120 BPM = 8 beats
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "kick_loop.wav";
        source.position = 0.0;
        source.offset = 0.0;
        source.length = 8.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Remove 1 beat from left
        ClipOperations::resizeContainerFromLeft(clip, 7.0);

        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.length == 7.0);
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(1.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(7.0));
        REQUIRE(clip.audioSources[0].position == 0.0);

        // Remove another beat from left
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(2.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(6.0));
        REQUIRE(clip.audioSources[0].position == 0.0);

        // Remove another beat from left
        ClipOperations::resizeContainerFromLeft(clip, 5.0);

        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.length == 5.0);
        REQUIRE(clip.audioSources[0].offset == Catch::Approx(3.0));
        REQUIRE(clip.audioSources[0].length == Catch::Approx(5.0));
        REQUIRE(clip.audioSources[0].position == 0.0);
    }

    SECTION("Alternating left and right resizes") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 6.0;
        clip.type = ClipType::Audio;

        AudioSource source;
        source.filePath = "test.wav";
        source.position = 1.0;  // 1s gap before audio
        source.offset = 0.0;
        source.length = 4.0;
        source.stretchFactor = 1.0;
        clip.audioSources.push_back(source);

        // Shrink from left by 1.0 (only removes empty space)
        ClipOperations::resizeContainerFromLeft(clip, 5.0);
        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.audioSources[0].position == Catch::Approx(0.0));  // 1.0 - 1.0
        REQUIRE(clip.audioSources[0].offset == 0.0);                   // No trim (was empty space)
        REQUIRE(clip.audioSources[0].length == 4.0);

        // Expand from right — source unchanged
        ClipOperations::resizeContainerFromRight(clip, 7.0);
        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.audioSources[0].position == 0.0);
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);

        // Expand from left — reveals space, source position increases
        ClipOperations::resizeContainerFromLeft(clip, 9.0);
        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.audioSources[0].position == Catch::Approx(2.0));  // 0.0 + 2.0
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);

        // Shrink from right — source unchanged
        ClipOperations::resizeContainerFromRight(clip, 5.0);
        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.audioSources[0].position == Catch::Approx(2.0));
        REQUIRE(clip.audioSources[0].offset == 0.0);
        REQUIRE(clip.audioSources[0].length == 4.0);
    }
}

// ============================================================================
// Visible region and file time calculation (waveform rendering math)
// ============================================================================

TEST_CASE("Waveform visible region calculation - time domain approach",
          "[clip][waveform][render]") {
    /**
     * Tests the time-domain waveform rendering math used in ClipComponent::paintAudioClip.
     *
     * The key insight: given a clip display length and source properties,
     * compute the visible region and file time window WITHOUT pixel intermediate steps.
     *
     * This avoids the integer rounding bug that caused alternating stretched/correct
     * waveform frames at low zoom levels.
     */

    SECTION("Source fills entire clip - visible region equals clip") {
        double clipDisplayLength = 4.0;
        double sourcePosition = 0.0;
        double sourceLength = 4.0;
        double sourceOffset = 0.0;
        double stretchFactor = 1.0;

        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 4.0);

        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Source starts after clip start - gap at beginning") {
        double clipDisplayLength = 8.0;
        double sourcePosition = 2.0;
        double sourceLength = 4.0;
        double sourceOffset = 0.0;
        double stretchFactor = 1.0;

        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        REQUIRE(visibleStart == 2.0);
        REQUIRE(visibleEnd == 6.0);

        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Source extends past clip end - clipped at right") {
        double clipDisplayLength = 3.0;
        double sourcePosition = 0.0;
        double sourceLength = 5.0;
        double sourceOffset = 0.0;
        double stretchFactor = 1.0;

        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 3.0);

        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 3.0);
    }

    SECTION("Source trimmed after left resize (offset advanced)") {
        // After resizeContainerFromLeft, source is trimmed:
        // offset advances, length shrinks, position stays 0.0
        double clipDisplayLength = 3.0;
        double sourcePosition = 0.0;
        double sourceLength = 3.0;  // Was 4.0, trimmed by 1.0
        double sourceOffset = 1.0;  // Was 0.0, advanced by 1.0
        double stretchFactor = 1.0;

        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 3.0);

        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        // File reads from 1.0 to 4.0 (same audio content as before trimming)
        REQUIRE(fileStart == Catch::Approx(1.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Stretched source - file times account for stretch factor") {
        double clipDisplayLength = 8.0;
        double sourcePosition = 0.0;
        double sourceLength = 8.0;
        double sourceOffset = 0.0;
        double stretchFactor = 2.0;  // 2x slower

        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 8.0);

        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        // 8 timeline seconds / 2.0 stretch = 4 file seconds
        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Source with offset and stretch") {
        double clipDisplayLength = 6.0;
        double sourcePosition = 0.0;
        double sourceLength = 6.0;
        double sourceOffset = 2.0;  // Start 2s into file
        double stretchFactor = 1.5;

        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        REQUIRE(fileStart == Catch::Approx(2.0));
        REQUIRE(fileEnd == Catch::Approx(2.0 + 6.0 / 1.5));  // 2.0 + 4.0 = 6.0
    }
}

TEST_CASE("Waveform visible region - drag preview simulation", "[clip][waveform][render][drag]") {
    /**
     * Tests the drag preview position simulation used during left resize drag.
     *
     * During a left resize drag, the clip length changes (previewLength) but
     * source.position hasn't been committed yet. The paint code simulates
     * the position adjustment: displaySourcePosition += (previewLength - dragStartLength)
     */
    SECTION("Left resize drag preview shifts source position") {
        // Initial state
        double sourcePosition = 0.0;
        double sourceLength = 4.0;
        double dragStartLength = 4.0;

        // User drags left edge to the right (shrinking clip from 4.0 to 3.0)
        double previewLength = 3.0;

        // Simulated display position (matches ClipComponent::paintAudioClip)
        double displaySourcePosition = sourcePosition + (previewLength - dragStartLength);

        // delta = 3.0 - 4.0 = -1.0, so displayPosition = 0.0 + (-1.0) = -1.0
        REQUIRE(displaySourcePosition == Catch::Approx(-1.0));

        // Visible region with simulated position
        double visibleStart = juce::jmax(displaySourcePosition, 0.0);  // 0.0
        double visibleEnd = juce::jmin(displaySourcePosition + sourceLength, previewLength);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 3.0);  // min(-1.0 + 4.0, 3.0) = min(3.0, 3.0) = 3.0

        // File time shows the correct 1.0s to 4.0s range (first second trimmed)
        double sourceOffset = 0.0;
        double stretchFactor = 1.0;
        double fileStart = sourceOffset + (visibleStart - displaySourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - displaySourcePosition) / stretchFactor;

        REQUIRE(fileStart == Catch::Approx(1.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Left resize drag preview - expanding clip") {
        double sourcePosition = 0.0;
        double sourceLength = 4.0;
        double dragStartLength = 4.0;

        // User drags left edge to the left (expanding clip from 4.0 to 6.0)
        double previewLength = 6.0;

        double displaySourcePosition = sourcePosition + (previewLength - dragStartLength);
        // delta = 6.0 - 4.0 = 2.0, so displayPosition = 0.0 + 2.0 = 2.0
        REQUIRE(displaySourcePosition == Catch::Approx(2.0));

        double visibleStart = juce::jmax(displaySourcePosition, 0.0);  // 2.0
        double visibleEnd = juce::jmin(displaySourcePosition + sourceLength, previewLength);
        // min(2.0 + 4.0, 6.0) = min(6.0, 6.0) = 6.0

        REQUIRE(visibleStart == 2.0);
        REQUIRE(visibleEnd == 6.0);

        // File time: full source visible, no trimming
        double sourceOffset = 0.0;
        double stretchFactor = 1.0;
        double fileStart = sourceOffset + (visibleStart - displaySourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - displaySourcePosition) / stretchFactor;

        REQUIRE(fileStart == Catch::Approx(0.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Right resize drag does NOT shift source position") {
        double sourcePosition = 0.0;
        double sourceLength = 4.0;

        // Right resize only changes clip length, no position simulation needed
        double previewLength = 3.0;

        // No adjustment for right resize
        double displaySourcePosition = sourcePosition;
        REQUIRE(displaySourcePosition == 0.0);

        double visibleStart = juce::jmax(displaySourcePosition, 0.0);
        double visibleEnd = juce::jmin(displaySourcePosition + sourceLength, previewLength);

        REQUIRE(visibleStart == 0.0);
        REQUIRE(visibleEnd == 3.0);  // Clipped at clip boundary

        double sourceOffset = 0.0;
        double stretchFactor = 1.0;
        double fileStart = sourceOffset + (visibleStart - displaySourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - displaySourcePosition) / stretchFactor;

        REQUIRE(fileStart == Catch::Approx(0.0));
        REQUIRE(fileEnd == Catch::Approx(3.0));
    }
}

// ============================================================================
// Pixel conversion consistency (the integer rounding regression)
// ============================================================================

TEST_CASE("Waveform pixel conversion - no stretch from rounding",
          "[clip][waveform][render][regression]") {
    /**
     * REGRESSION TEST
     *
     * Bug: At low zoom levels (e.g., 21 pixels/second), computing waveform bounds
     * via pixel→time→pixel round-trips introduced rounding errors that caused
     * the waveform to appear stretched on alternating frames.
     *
     * Fix: Compute visible region and file times entirely in the time domain,
     * only converting to pixels at the final step for drawing bounds.
     */
    SECTION("Low zoom: time-domain computation avoids rounding") {
        double pixelsPerSecond = 21.0;  // The exact zoom level from the bug report
        double clipDisplayLength = 4.0;
        int waveformWidth = static_cast<int>(clipDisplayLength * pixelsPerSecond + 0.5);  // 84

        double sourcePosition = 0.0;
        double sourceLength = 4.0;
        double sourceOffset = 0.0;
        double stretchFactor = 1.0;

        // Time-domain visible region
        double visibleStart = juce::jmax(sourcePosition, 0.0);
        double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

        // Pixel positions computed from time
        int drawX = static_cast<int>(visibleStart * pixelsPerSecond + 0.5);
        int drawRight = static_cast<int>(visibleEnd * pixelsPerSecond + 0.5);
        int drawWidth = drawRight - drawX;

        // Draw width should match waveform area width exactly
        REQUIRE(drawWidth == waveformWidth);

        // File times computed from time (not pixels)
        double fileStart = sourceOffset + (visibleStart - sourcePosition) / stretchFactor;
        double fileEnd = sourceOffset + (visibleEnd - sourcePosition) / stretchFactor;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Various zoom levels produce consistent draw width") {
        double clipDisplayLength = 4.0;
        double sourcePosition = 0.0;
        double sourceLength = 4.0;

        // Test zoom levels that caused issues
        std::vector<double> zoomLevels = {21.0, 15.0, 33.0, 47.0, 100.0, 200.0};

        for (double pps : zoomLevels) {
            int expectedWidth = static_cast<int>(clipDisplayLength * pps + 0.5);

            double visibleStart = juce::jmax(sourcePosition, 0.0);
            double visibleEnd = juce::jmin(sourcePosition + sourceLength, clipDisplayLength);

            int drawX = static_cast<int>(visibleStart * pps + 0.5);
            int drawRight = static_cast<int>(visibleEnd * pps + 0.5);
            int drawWidth = drawRight - drawX;

            REQUIRE(drawWidth == expectedWidth);
        }
    }

    SECTION("After right resize: draw width matches new clip length") {
        double pixelsPerSecond = 21.0;

        // Initial: 4 seconds
        double clipLength = 4.0;
        int width1 = static_cast<int>(clipLength * pixelsPerSecond + 0.5);

        // After resize to 3 seconds
        clipLength = 3.0;
        int width2 = static_cast<int>(clipLength * pixelsPerSecond + 0.5);

        // Widths should be different (not stretched)
        REQUIRE(width1 == 84);
        REQUIRE(width2 == 63);
        REQUIRE(width1 != width2);
    }
}
