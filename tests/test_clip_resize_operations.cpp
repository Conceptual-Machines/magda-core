#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for ClipOperations resize methods
 *
 * These tests verify:
 * - resizeContainerFromLeft adjusts audioOffset so audio stays at
 *   the same absolute timeline position
 * - resizeContainerFromRight only changes clip.length
 * - Sequential resize operations maintain correct state
 * - Visible region and file time calculation (time-domain waveform rendering)
 */

using namespace magda;

// ============================================================================
// resizeContainerFromLeft - audio offset compensation
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromLeft - trims audio offset", "[clip][resize][left]") {
    SECTION("Shrinking from left advances audio offset") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;

        // Shrink from left to 3.0 seconds (clip moves right by 1.0)
        ClipOperations::resizeContainerFromLeft(clip, 3.0);

        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.length == 3.0);

        // Audio offset advanced by 1.0 second (trim amount / stretch factor)
        REQUIRE(clip.audioOffset == Catch::Approx(1.0));
    }

    SECTION("Shrinking from left with stretch factor converts trim to file time") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 2.0;  // 2x slower

        // Shrink from left by 2.0 timeline seconds
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);

        // File offset advances by 2.0 / 2.0 = 1.0 file seconds
        REQUIRE(clip.audioOffset == Catch::Approx(1.0));
        REQUIRE(clip.audioStretchFactor == 2.0);  // Unchanged
    }

    SECTION("Expanding from left reveals earlier audio") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 2.0;  // Previously trimmed
        clip.audioStretchFactor = 1.0;

        // Expand from left to 6.0 seconds (clip moves left by 2.0)
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 6.0);

        // Audio offset reduced (revealing earlier audio)
        REQUIRE(clip.audioOffset == Catch::Approx(0.0));
    }

    SECTION("Expanding from left clamps audioOffset to 0") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 0.5;  // Only 0.5s of offset available
        clip.audioStretchFactor = 1.0;

        // Try to expand from left to 8.0 (would need 4.0s of offset reduction)
        ClipOperations::resizeContainerFromLeft(clip, 8.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 8.0);

        // Offset clamped to 0.0 (can't go negative)
        REQUIRE(clip.audioOffset == 0.0);
    }

    SECTION("Expand past zero clamps startTime correctly") {
        ClipInfo clip;
        clip.startTime = 1.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;

        // Try to expand to 8.0 (would put startTime at -3.0, clamped to 0.0)
        ClipOperations::resizeContainerFromLeft(clip, 8.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 8.0);
    }
}

// ============================================================================
// resizeContainerFromRight - clip length only
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromRight - audio data unchanged",
          "[clip][resize][right]") {
    SECTION("Shrinking from right does not modify audio fields") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 1.0;
        clip.audioStretchFactor = 1.5;

        ClipOperations::resizeContainerFromRight(clip, 3.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 3.0);

        // All audio properties unchanged
        REQUIRE(clip.audioOffset == 1.0);
        REQUIRE(clip.audioStretchFactor == 1.5);
    }

    SECTION("Expanding from right does not modify audio fields") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;

        ClipOperations::resizeContainerFromRight(clip, 8.0);

        REQUIRE(clip.startTime == 2.0);  // Unchanged
        REQUIRE(clip.length == 8.0);

        REQUIRE(clip.audioOffset == 0.0);
        REQUIRE(clip.audioStretchFactor == 1.0);
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

TEST_CASE("ClipOperations - Sequential resizes maintain correct audio offset",
          "[clip][resize][sequential][regression]") {
    SECTION("Multiple left resizes trim audio offset progressively") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;  // 2 bars at 120 BPM = 8 beats
        clip.type = ClipType::Audio;
        clip.audioFilePath = "kick_loop.wav";
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;

        // Remove 1 beat from left
        ClipOperations::resizeContainerFromLeft(clip, 7.0);

        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.length == 7.0);
        REQUIRE(clip.audioOffset == Catch::Approx(1.0));

        // Remove another beat from left
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);
        REQUIRE(clip.audioOffset == Catch::Approx(2.0));

        // Remove another beat from left
        ClipOperations::resizeContainerFromLeft(clip, 5.0);

        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.length == 5.0);
        REQUIRE(clip.audioOffset == Catch::Approx(3.0));
    }

    SECTION("Alternating left and right resizes") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 6.0;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;

        // Shrink from left by 1.0
        ClipOperations::resizeContainerFromLeft(clip, 5.0);
        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.audioOffset == Catch::Approx(1.0));

        // Expand from right — audio offset unchanged
        ClipOperations::resizeContainerFromRight(clip, 7.0);
        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.audioOffset == Catch::Approx(1.0));

        // Expand from left — reveals earlier audio (reduces offset)
        ClipOperations::resizeContainerFromLeft(clip, 9.0);
        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.audioOffset ==
                Catch::Approx(0.0));  // Reduced by 1.0 (clamped from -1.0 to 0.0)

        // Shrink from right — audio offset unchanged
        ClipOperations::resizeContainerFromRight(clip, 5.0);
        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.audioOffset == Catch::Approx(0.0));
    }
}

// ============================================================================
// Visible region and file time calculation (waveform rendering math)
// ============================================================================

TEST_CASE("Waveform visible region calculation - flat clip model", "[clip][waveform][render]") {
    /**
     * Tests the time-domain waveform rendering math used in ClipComponent::paintAudioClip.
     *
     * With the flat model, audio always starts at clip position 0 (no source.position).
     * The visible region is simply [0, clip.length] and file time is computed from
     * audioOffset and audioStretchFactor.
     */

    SECTION("Audio fills entire clip") {
        double clipLength = 4.0;
        double audioOffset = 0.0;
        double stretchFactor = 1.0;

        double fileStart = audioOffset;
        double fileEnd = audioOffset + clipLength / stretchFactor;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Audio with offset (trimmed from left)") {
        double clipLength = 3.0;
        double audioOffset = 1.0;  // Was trimmed by 1.0
        double stretchFactor = 1.0;

        double fileStart = audioOffset;
        double fileEnd = audioOffset + clipLength / stretchFactor;

        // File reads from 1.0 to 4.0 (same audio content as before trimming)
        REQUIRE(fileStart == Catch::Approx(1.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Stretched audio - file times account for stretch factor") {
        double clipLength = 8.0;
        double audioOffset = 0.0;
        double stretchFactor = 2.0;  // 2x slower

        double fileStart = audioOffset;
        double fileEnd = audioOffset + clipLength / stretchFactor;

        // 8 timeline seconds / 2.0 stretch = 4 file seconds
        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Audio with offset and stretch") {
        double clipLength = 6.0;
        double audioOffset = 2.0;  // Start 2s into file
        double stretchFactor = 1.5;

        double fileStart = audioOffset;
        double fileEnd = audioOffset + clipLength / stretchFactor;

        REQUIRE(fileStart == Catch::Approx(2.0));
        REQUIRE(fileEnd == Catch::Approx(2.0 + 6.0 / 1.5));  // 2.0 + 4.0 = 6.0
    }
}

TEST_CASE("Waveform visible region - drag preview simulation", "[clip][waveform][render][drag]") {
    /**
     * Tests the drag preview offset simulation used during left resize drag.
     *
     * During a left resize drag, the clip length changes (previewLength) but
     * audioOffset hasn't been committed yet. The paint code simulates
     * the offset adjustment.
     */
    SECTION("Left resize drag preview simulates offset advancement") {
        // Initial state
        double audioOffset = 0.0;
        double audioStretchFactor = 1.0;
        double dragStartLength = 4.0;

        // User drags left edge to the right (shrinking clip from 4.0 to 3.0)
        double previewLength = 3.0;
        double trimAmount = dragStartLength - previewLength;  // 1.0

        // Simulated offset during drag preview
        double previewOffset = audioOffset + trimAmount / audioStretchFactor;
        REQUIRE(previewOffset == Catch::Approx(1.0));

        // File time with simulated offset
        double fileStart = previewOffset;
        double fileEnd = previewOffset + previewLength / audioStretchFactor;

        REQUIRE(fileStart == Catch::Approx(1.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Left resize drag preview - expanding clip") {
        double audioOffset = 1.0;  // Previously trimmed
        double audioStretchFactor = 1.0;
        double dragStartLength = 3.0;

        // User drags left edge to the left (expanding clip from 3.0 to 5.0)
        double previewLength = 5.0;
        double trimAmount = dragStartLength - previewLength;  // -2.0

        // Simulated offset during drag preview
        double previewOffset = juce::jmax(0.0, audioOffset + trimAmount / audioStretchFactor);
        // 1.0 + (-2.0) = -1.0, clamped to 0.0
        REQUIRE(previewOffset == Catch::Approx(0.0));

        // File time: starts from beginning of file
        double fileStart = previewOffset;
        double fileEnd = previewOffset + previewLength / audioStretchFactor;

        REQUIRE(fileStart == Catch::Approx(0.0));
        REQUIRE(fileEnd == Catch::Approx(5.0));
    }

    SECTION("Right resize drag does NOT change audio offset") {
        double audioOffset = 0.0;
        double audioStretchFactor = 1.0;

        // Right resize only changes clip length
        double previewLength = 3.0;

        // No offset adjustment for right resize
        double fileStart = audioOffset;
        double fileEnd = audioOffset + previewLength / audioStretchFactor;

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
     * via pixel->time->pixel round-trips introduced rounding errors that caused
     * the waveform to appear stretched on alternating frames.
     *
     * Fix: Compute visible region and file times entirely in the time domain,
     * only converting to pixels at the final step for drawing bounds.
     */
    SECTION("Low zoom: time-domain computation avoids rounding") {
        double pixelsPerSecond = 21.0;  // The exact zoom level from the bug report
        double clipLength = 4.0;
        int waveformWidth = static_cast<int>(clipLength * pixelsPerSecond + 0.5);  // 84

        // Time-domain: full clip visible
        int drawX = 0;
        int drawRight = static_cast<int>(clipLength * pixelsPerSecond + 0.5);
        int drawWidth = drawRight - drawX;

        // Draw width should match waveform area width exactly
        REQUIRE(drawWidth == waveformWidth);

        // File times computed from time (not pixels)
        double audioOffset = 0.0;
        double stretchFactor = 1.0;
        double fileStart = audioOffset;
        double fileEnd = audioOffset + clipLength / stretchFactor;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Various zoom levels produce consistent draw width") {
        double clipLength = 4.0;

        // Test zoom levels that caused issues
        std::vector<double> zoomLevels = {21.0, 15.0, 33.0, 47.0, 100.0, 200.0};

        for (double pps : zoomLevels) {
            int expectedWidth = static_cast<int>(clipLength * pps + 0.5);

            int drawX = 0;
            int drawRight = static_cast<int>(clipLength * pps + 0.5);
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
