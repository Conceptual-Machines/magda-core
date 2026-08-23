#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for ClipOperations resize methods
 *
 * These tests verify:
 * - resizeContainerFromLeft adjusts offset so audio stays at
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
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Shrink from left to 3.0 seconds (clip moves right by 1.0)
        ClipOperations::resizeContainerFromLeft(clip, 3.0);

        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.length == 3.0);

        // Audio offset advanced by 1.0 second (trim amount * speedRatio)
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));
    }

    SECTION("Shrinking from left with speed ratio converts trim to file time") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio =
            2.0;  // 2x faster (speedRatio = speed factor semantics)

        // Shrink from left by 2.0 timeline seconds
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);

        // File offset advances by 2.0 * 2.0 = 4.0 file seconds
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(4.0));
        REQUIRE(magda::test::audioEvent(clip).speedRatio == 2.0);  // Unchanged
    }

    SECTION("Expanding from left reveals earlier audio") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(2.0);  // Previously trimmed
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Expand from left to 6.0 seconds (clip moves left by 2.0)
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 6.0);

        // Audio offset reduced (revealing earlier audio)
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(0.0));
    }

    SECTION("Expanding from left clamps offset to 0") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.5);  // Only 0.5s of offset available
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Try to expand from left to 8.0 (would need 4.0s of offset reduction)
        ClipOperations::resizeContainerFromLeft(clip, 8.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 8.0);

        // Offset clamped to 0.0 (can't go negative)
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == 0.0);
    }

    SECTION("Expand past zero clamps startTime correctly") {
        ClipInfo clip;
        clip.startTime = 1.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Try to expand to 8.0 (would put startTime at -3.0, clamped to 0.0)
        ClipOperations::resizeContainerFromLeft(clip, 8.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 8.0);
    }
}

TEST_CASE("ClipOperations placement edits keep beats authoritative", "[clip][resize][beats]") {
    ClipInfo clip;
    clip.startTime = 99.0;
    clip.length = 99.0;
    clip.setPlacementBeats(8.0, 4.0);

    ClipOperations::resizeContainerFromRight(clip, 3.0, 120.0);

    REQUIRE(clip.placement.startBeat == Catch::Approx(8.0));
    REQUIRE(clip.placement.lengthBeats == Catch::Approx(6.0));
    REQUIRE(clip.startTime == Catch::Approx(4.0));
    REQUIRE(clip.length == Catch::Approx(3.0));

    ClipOperations::setTimelinePlacement(clip, 6.0, 2.0, 120.0);

    REQUIRE(clip.placement.startBeat == Catch::Approx(12.0));
    REQUIRE(clip.placement.lengthBeats == Catch::Approx(4.0));
    REQUIRE(clip.startTime == Catch::Approx(6.0));
    REQUIRE(clip.length == Catch::Approx(2.0));
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
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(1.0);
        magda::test::audioEvent(clip).speedRatio = 1.5;

        ClipOperations::resizeContainerFromRight(clip, 3.0);

        REQUIRE(clip.startTime == 0.0);
        REQUIRE(clip.length == 3.0);

        // All audio properties unchanged
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == 1.0);
        REQUIRE(magda::test::audioEvent(clip).speedRatio == 1.5);
    }

    SECTION("Expanding from right does not modify audio fields") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        ClipOperations::resizeContainerFromRight(clip, 8.0);

        REQUIRE(clip.startTime == 2.0);  // Unchanged
        REQUIRE(clip.length == 8.0);

        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == 0.0);
        REQUIRE(magda::test::audioEvent(clip).speedRatio == 1.0);
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
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "kick_loop.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Remove 1 beat from left
        ClipOperations::resizeContainerFromLeft(clip, 7.0);

        REQUIRE(clip.startTime == 1.0);
        REQUIRE(clip.length == 7.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));

        // Remove another beat from left
        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(2.0));

        // Remove another beat from left
        ClipOperations::resizeContainerFromLeft(clip, 5.0);

        REQUIRE(clip.startTime == 3.0);
        REQUIRE(clip.length == 5.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(3.0));
    }

    SECTION("Alternating left and right resizes") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 6.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Shrink from left by 1.0
        ClipOperations::resizeContainerFromLeft(clip, 5.0);
        REQUIRE(clip.startTime == 3.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));

        // Expand from right — audio offset unchanged
        ClipOperations::resizeContainerFromRight(clip, 7.0);
        REQUIRE(clip.startTime == 3.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));

        // Expand from left — reveals earlier audio (reduces offset)
        ClipOperations::resizeContainerFromLeft(clip, 9.0);
        REQUIRE(clip.startTime == 1.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() ==
                Catch::Approx(0.0));  // Reduced by 1.0 (clamped from -1.0 to 0.0)

        // Shrink from right — audio offset unchanged
        ClipOperations::resizeContainerFromRight(clip, 5.0);
        REQUIRE(clip.startTime == 1.0);
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(0.0));
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
     * offset and speedRatio.
     */

    SECTION("Audio fills entire clip") {
        double clipLength = 4.0;
        double offset = 0.0;
        double speedRatio = 1.0;

        double fileStart = offset;
        double fileEnd = offset + clipLength * speedRatio;

        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Audio with offset (trimmed from left)") {
        double clipLength = 3.0;
        double offset = 1.0;  // Was trimmed by 1.0
        double speedRatio = 1.0;

        double fileStart = offset;
        double fileEnd = offset + clipLength * speedRatio;

        // File reads from 1.0 to 4.0 (same audio content as before trimming)
        REQUIRE(fileStart == Catch::Approx(1.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Stretched audio - file times account for speed ratio") {
        double clipLength = 2.0;
        double offset = 0.0;
        double speedRatio = 2.0;  // 2x faster (speedRatio = speed factor semantics)

        double fileStart = offset;
        double fileEnd = offset + clipLength * speedRatio;

        // 2 timeline seconds * 2.0 speedRatio = 4 file seconds
        REQUIRE(fileStart == 0.0);
        REQUIRE(fileEnd == 4.0);
    }

    SECTION("Audio with offset and speed ratio") {
        double clipLength = 4.0;
        double offset = 2.0;      // Start 2s into file
        double speedRatio = 1.5;  // 1.5x faster

        double fileStart = offset;
        double fileEnd = offset + clipLength * speedRatio;

        REQUIRE(fileStart == Catch::Approx(2.0));
        REQUIRE(fileEnd == Catch::Approx(2.0 + 4.0 * 1.5));  // 2.0 + 6.0 = 8.0
    }
}

TEST_CASE("Waveform visible region - drag preview simulation", "[clip][waveform][render][drag]") {
    /**
     * Tests the drag preview offset simulation used during left resize drag.
     *
     * During a left resize drag, the clip length changes (previewLength) but
     * offset hasn't been committed yet. The paint code simulates
     * the offset adjustment.
     */
    SECTION("Left resize drag preview simulates offset advancement") {
        // Initial state
        double offset = 0.0;
        double speedRatio = 1.0;
        double dragStartLength = 4.0;

        // User drags left edge to the right (shrinking clip from 4.0 to 3.0)
        double previewLength = 3.0;
        double trimAmount = dragStartLength - previewLength;  // 1.0

        // Simulated offset during drag preview
        double previewOffset = offset + trimAmount * speedRatio;
        REQUIRE(previewOffset == Catch::Approx(1.0));

        // File time with simulated offset
        double fileStart = previewOffset;
        double fileEnd = previewOffset + previewLength * speedRatio;

        REQUIRE(fileStart == Catch::Approx(1.0));
        REQUIRE(fileEnd == Catch::Approx(4.0));
    }

    SECTION("Left resize drag preview - expanding clip") {
        double offset = 1.0;  // Previously trimmed
        double speedRatio = 1.0;
        double dragStartLength = 3.0;

        // User drags left edge to the left (expanding clip from 3.0 to 5.0)
        double previewLength = 5.0;
        double trimAmount = dragStartLength - previewLength;  // -2.0

        // Simulated offset during drag preview
        double previewOffset = juce::jmax(0.0, offset + trimAmount * speedRatio);
        // 1.0 + (-2.0) = -1.0, clamped to 0.0
        REQUIRE(previewOffset == Catch::Approx(0.0));

        // File time: starts from beginning of file
        double fileStart = previewOffset;
        double fileEnd = previewOffset + previewLength * speedRatio;

        REQUIRE(fileStart == Catch::Approx(0.0));
        REQUIRE(fileEnd == Catch::Approx(5.0));
    }

    SECTION("Right resize drag does NOT change audio offset") {
        double offset = 0.0;
        double speedRatio = 1.0;

        // Right resize only changes clip length
        double previewLength = 3.0;

        // No offset adjustment for right resize
        double fileStart = offset;
        double fileEnd = offset + previewLength * speedRatio;

        REQUIRE(fileStart == Catch::Approx(0.0));
        REQUIRE(fileEnd == Catch::Approx(3.0));
    }
}

// ============================================================================
// Throttled drag simulation (the offset shift regression)
// ============================================================================

TEST_CASE("Left resize with throttled drag updates - offset must use original state",
          "[clip][resize][left][regression]") {
    /**
     * REGRESSION TEST
     *
     * Bug: When resizing a non-looped audio clip from the left edge, audio
     * content shifts by ~2 beats instead of staying aligned.
     *
     * Root cause: During drag, throttled updates modify clip.startTime and
     * clip.length but NOT magda::test::audioEvent(clip).anchorSeconds(). On mouseUp, the resize
     * commit calls ClipOperations::resizeContainerFromLeft() which expects to operate on pre-drag
     * state but receives post-drag state. The offset calculation uses the already-modified
     * startTime, resulting in wrong delta.
     *
     * Fix: The mouseUp handler must compute offset adjustment from
     * the ORIGINAL clip state captured at mouseDown, not from the
     * throttle-modified current state.
     */

    SECTION("Simulated throttled drag: offset calculated from original state") {
        // Original clip state at mouseDown
        ClipInfo originalState;
        originalState.startTime = 0.0;
        originalState.length = 4.0;
        originalState.setAudioContent();
        magda::test::giveAudioEvent(originalState, "test.wav");
        magda::test::audioEvent(originalState).setAnchorSeconds(0.0);
        magda::test::audioEvent(originalState).speedRatio = 1.0;

        // Simulate throttled drag updates (what ClipComponent does during drag)
        // These modify startTime and length but NOT offset
        ClipInfo throttleModifiedState = originalState;
        double finalStartTime = 1.0;  // User dragged left edge right by 1 second
        double finalLength = 3.0;     // New length after resize

        throttleModifiedState.startTime = finalStartTime;
        throttleModifiedState.length = finalLength;
        // Note: offset is NOT modified during drag

        // WRONG approach (the bug): calculate delta from throttle-modified state
        double buggyDelta = throttleModifiedState.startTime - throttleModifiedState.startTime;
        // This is always 0.0! The calculation compares new startTime to itself.
        REQUIRE(buggyDelta == 0.0);  // Bug: no offset adjustment

        // CORRECT approach (the fix): calculate delta from ORIGINAL state
        double correctDelta = finalStartTime - originalState.startTime;  // 1.0 - 0.0 = 1.0
        double correctOffset = magda::test::audioEvent(originalState).anchorSeconds() +
                               correctDelta / magda::test::audioEvent(originalState).speedRatio;

        REQUIRE(correctDelta == Catch::Approx(1.0));
        REQUIRE(correctOffset == Catch::Approx(1.0));

        // Verify final length calculation is consistent
        double expectedFinalLength = originalState.length - correctDelta;
        REQUIRE(expectedFinalLength == Catch::Approx(finalLength));
    }

    SECTION("Simulated throttled drag with speed ratio") {
        // Original clip state at mouseDown
        ClipInfo originalState;
        originalState.startTime = 0.0;
        originalState.length = 8.0;
        originalState.setAudioContent();
        magda::test::giveAudioEvent(originalState, "test.wav");
        magda::test::audioEvent(originalState).setAnchorSeconds(0.0);
        magda::test::audioEvent(originalState).speedRatio =
            2.0;  // 2x slower (speedRatio = stretchFactor semantics)

        // User drags left edge right by 2 timeline seconds
        double finalStartTime = 2.0;

        // CORRECT approach: calculate from original state
        double correctDelta = finalStartTime - originalState.startTime;  // 2.0
        double correctOffset = magda::test::audioEvent(originalState).anchorSeconds() +
                               correctDelta / magda::test::audioEvent(originalState).speedRatio;

        // 2.0 timeline seconds / 2.0 speedRatio = 1.0 file second offset
        REQUIRE(correctOffset == Catch::Approx(1.0));
    }

    SECTION("Multiple throttled updates accumulate correctly when using original state") {
        // Original clip state at mouseDown
        ClipInfo originalState;
        originalState.startTime = 0.0;
        originalState.length = 8.0;
        originalState.setAudioContent();
        magda::test::giveAudioEvent(originalState, "test.wav");
        magda::test::audioEvent(originalState).setAnchorSeconds(0.0);
        magda::test::audioEvent(originalState).speedRatio = 1.0;

        // Simulate multiple throttled updates during drag
        // User drags: 0.5s, then 1.0s, then 1.5s, finally 2.0s
        std::vector<double> dragPositions = {0.5, 1.0, 1.5, 2.0};

        // Each throttle update modifies the "current" state
        ClipInfo currentState = originalState;

        for (double pos : dragPositions) {
            currentState.startTime = pos;
            currentState.length = originalState.length - pos;
            // offset not modified during drag
        }

        // Final state after drag
        double finalStartTime = currentState.startTime;  // 2.0

        // CORRECT: Use original state for offset calculation
        double correctDelta = finalStartTime - originalState.startTime;
        double correctOffset = magda::test::audioEvent(originalState).anchorSeconds() +
                               correctDelta / magda::test::audioEvent(originalState).speedRatio;

        REQUIRE(finalStartTime == 2.0);
        REQUIRE(correctOffset == Catch::Approx(2.0));
    }

    SECTION("Expanding from left with throttled drag") {
        // Clip was previously trimmed
        ClipInfo originalState;
        originalState.startTime = 2.0;
        originalState.length = 4.0;
        originalState.setAudioContent();
        magda::test::giveAudioEvent(originalState, "test.wav");
        magda::test::audioEvent(originalState).setAnchorSeconds(2.0);  // Previously trimmed
        magda::test::audioEvent(originalState).speedRatio = 1.0;

        // User drags left edge LEFT (expanding) by 2 seconds
        double finalStartTime = 0.0;

        // CORRECT: Calculate from original state
        double correctDelta = finalStartTime - originalState.startTime;  // -2.0
        double correctOffset =
            juce::jmax(0.0, magda::test::audioEvent(originalState).anchorSeconds() +
                                correctDelta / magda::test::audioEvent(originalState).speedRatio);

        // 2.0 - 2.0 = 0.0 (reveals audio from beginning)
        REQUIRE(correctOffset == Catch::Approx(0.0));

        // Verify the delta is negative (expanding)
        REQUIRE(correctDelta == Catch::Approx(-2.0));
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
        double offset = 0.0;
        double speedRatio = 1.0;
        double fileStart = offset;
        double fileEnd = offset + clipLength * speedRatio;

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

// ============================================================================
// loopStart = offset invariant for non-looped clips
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromLeft - loopStart tracks offset for non-looped clips",
          "[clip][resize][left][loopstart]") {
    SECTION("Shrink from left: loopStart equals offset after resize") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        clip.loopEnabled = false;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        ClipOperations::resizeContainerFromLeft(clip, 3.0);

        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }

    SECTION("Expand from left: loopStart equals offset after resize") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(2.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(2.0);
        clip.loopEnabled = false;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(0.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }

    SECTION("Multiple left resizes: loopStart always tracks offset") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        clip.loopEnabled = false;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        for (int i = 0; i < 5; ++i) {
            ClipOperations::resizeContainerFromLeft(clip, clip.length - 1.0);
            REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                    Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
        }
    }

    SECTION("With speed ratio: loopStart tracks offset correctly") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        clip.loopEnabled = false;
        magda::test::audioEvent(clip).speedRatio = 2.0;

        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        // 2.0 timeline delta * 2.0 speedRatio = 4.0 source offset
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(4.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }
}

// ============================================================================
// Looped resize: loopStart must NOT change
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromLeft - loopStart unchanged for looped clips",
          "[clip][resize][left][loopstart][loop]") {
    SECTION("Shrink from left: loopStart stays at user-defined position") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(1.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.5);
        magda::test::audioEvent(clip).setLoopLengthSeconds(2.0);
        clip.loopEnabled = true;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        double originalLoopStart = magda::test::audioEvent(clip).loopStartSeconds();

        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        // loopStart must NOT change — it's the user-defined loop anchor
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(originalLoopStart));
        // offset should have been adjusted (wrapped within loop region)
        REQUIRE(clip.startTime == 2.0);
        REQUIRE(clip.length == 6.0);
    }

    SECTION("Expand from left: loopStart stays at user-defined position") {
        ClipInfo clip;
        clip.startTime = 4.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(1.5);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.5);
        magda::test::audioEvent(clip).setLoopLengthSeconds(2.0);
        clip.loopEnabled = true;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        double originalLoopStart = magda::test::audioEvent(clip).loopStartSeconds();

        ClipOperations::resizeContainerFromLeft(clip, 6.0);

        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(originalLoopStart));
    }

    SECTION("Multiple looped resizes: loopStart never changes") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(1.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.5);
        magda::test::audioEvent(clip).setLoopLengthSeconds(2.0);
        clip.loopEnabled = true;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        double originalLoopStart = magda::test::audioEvent(clip).loopStartSeconds();

        // Shrink
        ClipOperations::resizeContainerFromLeft(clip, 6.0);
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(originalLoopStart));

        // Shrink more
        ClipOperations::resizeContainerFromLeft(clip, 4.0);
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(originalLoopStart));

        // Expand
        ClipOperations::resizeContainerFromLeft(clip, 7.0);
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(originalLoopStart));
    }

    SECTION("Looped offset wraps within loop region") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(1.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        magda::test::audioEvent(clip).setLoopLengthSeconds(2.0);
        clip.loopEnabled = true;
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Shrink by 3 seconds — phaseDelta = 3.0, wraps within loopLength=2.0
        ClipOperations::resizeContainerFromLeft(clip, 5.0);

        // offset should wrap: relOffset = 1.0-0.0 = 1.0, phaseDelta = 3.0
        // wrapPhase(1.0 + 3.0, 2.0) = wrapPhase(4.0, 2.0) = 0.0
        // new offset = loopStart + 0.0 = 0.0
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(0.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(0.0));  // Unchanged
    }
}

// ============================================================================
// trimAudioFromLeft: loopStart = offset invariant
// ============================================================================

TEST_CASE("ClipOperations::trimAudioFromLeft - loopStart tracks offset",
          "[clip][trim][left][loopstart]") {
    SECTION("Trim inward: loopStart equals offset") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        ClipOperations::trimAudioFromLeft(clip, 1.0);

        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }

    SECTION("Trim outward (extend): loopStart equals offset") {
        ClipInfo clip;
        clip.startTime = 2.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(2.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(2.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        ClipOperations::trimAudioFromLeft(clip, -1.0);

        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }

    SECTION("Trim with speed ratio: loopStart equals offset") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.5;

        ClipOperations::trimAudioFromLeft(clip, 2.0);

        // sourceDelta = 2.0 * 1.5 = 3.0
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(3.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }

    SECTION("Trim clamps to zero: loopStart equals offset") {
        ClipInfo clip;
        clip.startTime = 1.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.5);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.5);
        magda::test::audioEvent(clip).speedRatio = 1.0;

        // Try to extend past start of file
        ClipOperations::trimAudioFromLeft(clip, -2.0);

        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(0.0));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(clip).anchorSeconds()));
    }
}

// ============================================================================
// Auto-tempo (beat mode) audio clips: use BPM ratio, not speedRatio
// ============================================================================

TEST_CASE("ClipOperations::resizeContainerFromLeft - auto-tempo offset uses BPM ratio",
          "[clip][resize][left][autotempo]") {
    SECTION("Non-looped auto-tempo: offset uses beats as authoritative") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setAnchorBeats(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;
        magda::test::audioEvent(clip).autoTempo = true;
        magda::test::audioEvent(clip).interpBpm = 140.0;

        // Shrink by 1 second at 120 BPM
        ClipOperations::resizeContainerFromLeft(clip, 3.0, 120.0);

        // deltaBeats = 1.0 * 120/60 = 2.0 beats
        // offsetBeats = 0 + 2.0 = 2.0
        // offset (seconds) = 2.0 * 60/140 = 6/7
        REQUIRE(magda::test::audioEvent(clip).anchorBeats() == Catch::Approx(2.0));
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(120.0 / 140.0));
    }

    SECTION("Looped auto-tempo: offset wraps using beats") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).setAnchorBeats(0.0);
        magda::test::audioEvent(clip).setLoopStartSeconds(0.0);
        magda::test::audioEvent(clip).setLoopStartBeats(0.0);
        magda::test::audioEvent(clip).setLoopLengthSeconds(2.0);               // 2 source seconds
        magda::test::audioEvent(clip).setLoopLengthBeats(2.0 * 140.0 / 60.0);  // source beats
        clip.loopEnabled = true;
        magda::test::audioEvent(clip).speedRatio = 1.0;
        magda::test::audioEvent(clip).autoTempo = true;
        magda::test::audioEvent(clip).interpBpm = 140.0;

        // Shrink by 1 second at 120 BPM
        ClipOperations::resizeContainerFromLeft(clip, 7.0, 120.0);

        // deltaBeats = 1.0 * 120/60 = 2.0 project beats
        // wrapPhase(0 + 2.0, 4.667) = 2.0 beats
        // offset (seconds) = 2.0 * 60/140 = 6/7
        REQUIRE(magda::test::audioEvent(clip).anchorBeats() == Catch::Approx(2.0));
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(120.0 / 140.0));
    }

    SECTION("Non-auto-tempo still uses speedRatio") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 2.0;
        magda::test::audioEvent(clip).autoTempo = false;
        magda::test::audioEvent(clip).interpBpm = 140.0;

        ClipOperations::resizeContainerFromLeft(clip, 3.0, 120.0);

        // Should use speedRatio (2.0), not BPM ratio
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(2.0));
    }

    SECTION("Auto-tempo with matching BPMs gives same result as speedRatio=1") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.setAudioContent();
        magda::test::giveAudioEvent(clip, "test.wav");
        magda::test::audioEvent(clip).setAnchorSeconds(0.0);
        magda::test::audioEvent(clip).speedRatio = 1.0;
        magda::test::audioEvent(clip).autoTempo = true;
        magda::test::audioEvent(clip).interpBpm = 120.0;  // Same as project

        ClipOperations::resizeContainerFromLeft(clip, 3.0, 120.0);

        // 120/120 = 1.0, same as speedRatio
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() == Catch::Approx(1.0));
    }
}

// ============================================================================
// Multi-clip left resize preview (#1950)
// ============================================================================

TEST_CASE("Multi-clip left-resize preview recomputes every clip from its snapshot",
          "[clip][resize][left][multi][regression]") {
    /**
     * REGRESSION TEST (#1950)
     *
     * Dragging the left handle of a multi-clip selection previewed only the
     * dragged clip; the rest jumped into place on mouse release. The drag now
     * applies the same length delta to every selected clip on each throttled
     * tick.
     *
     * The invariant that makes that safe: each tick recomputes from the
     * clip's PRE-DRAG snapshot, never from its current (already previewed)
     * state. Applying the delta to live state accumulates drift, and for MIDI
     * it accumulates midiTrimOffset once per tick.
     *
     * This mirrors the loop in ClipComponent::mouseDrag / DragMode::ResizeLeft.
     */

    const double bpm = 120.0;

    // One throttled drag tick: rebuild the preview from the snapshot.
    auto previewTick = [bpm](const ClipInfo& snapshot, double originalLength, double lengthDelta) {
        ClipInfo preview = snapshot;
        ClipOperations::resizeContainerFromLeft(preview,
                                                juce::jmax(0.1, originalLength + lengthDelta), bpm);
        if (auto* event = preview.primaryEvent(); event != nullptr && !preview.loopEnabled)
            event->loopStartSamples = event->sourceAnchorSamples;
        return preview;
    };

    SECTION("Every selected clip shifts by the same delta, whatever its own start") {
        ClipInfo dragged;
        magda::test::giveAudioEvent(dragged, "a.wav");
        ClipOperations::setTimelinePlacement(dragged, 0.0, 4.0, bpm);
        magda::test::audioEvent(dragged).setAnchorSeconds(2.0);

        ClipInfo other;
        magda::test::giveAudioEvent(other, "b.wav");
        ClipOperations::setTimelinePlacement(other, 10.0, 6.0, bpm);
        magda::test::audioEvent(other).setAnchorSeconds(3.0);

        // Drag the left handle right by 1 second: both clips shrink by 1
        const double lengthDelta = -1.0;

        ClipInfo draggedPreview = previewTick(dragged, 4.0, lengthDelta);
        ClipInfo otherPreview = previewTick(other, 6.0, lengthDelta);

        REQUIRE(draggedPreview.getTimelineStart(bpm) == Catch::Approx(1.0));
        REQUIRE(draggedPreview.getTimelineLength(bpm) == Catch::Approx(3.0));
        REQUIRE(otherPreview.getTimelineStart(bpm) == Catch::Approx(11.0));
        REQUIRE(otherPreview.getTimelineLength(bpm) == Catch::Approx(5.0));

        // Ends stay pinned — left resize only moves the start
        REQUIRE(draggedPreview.getTimelineEnd(bpm) == Catch::Approx(4.0));
        REQUIRE(otherPreview.getTimelineEnd(bpm) == Catch::Approx(16.0));

        // Each clip trims its own audio from its own offset
        REQUIRE(magda::audioEventRef(draggedPreview).anchorSeconds() == Catch::Approx(3.0));
        REQUIRE(magda::audioEventRef(otherPreview).anchorSeconds() == Catch::Approx(4.0));
        REQUIRE(magda::audioEventRef(draggedPreview).loopStartSeconds() ==
                Catch::Approx(magda::audioEventRef(draggedPreview).anchorSeconds()));
        REQUIRE(magda::audioEventRef(otherPreview).loopStartSeconds() ==
                Catch::Approx(magda::audioEventRef(otherPreview).anchorSeconds()));
    }

    SECTION("Successive ticks do not drift - final tick equals a single-shot resize") {
        ClipInfo snapshot;
        snapshot.setAudioContent();
        magda::test::giveAudioEvent(snapshot, "b.wav");
        ClipOperations::setTimelinePlacement(snapshot, 10.0, 6.0, bpm);
        magda::test::audioEvent(snapshot).setAnchorSeconds(3.0);

        ClipInfo preview = snapshot;
        for (double delta : {-0.25, -0.5, -0.75, -1.0})
            preview = previewTick(snapshot, 6.0, delta);

        ClipInfo singleShot = previewTick(snapshot, 6.0, -1.0);

        REQUIRE(preview.getTimelineStart(bpm) == Catch::Approx(singleShot.getTimelineStart(bpm)));
        REQUIRE(preview.getTimelineLength(bpm) == Catch::Approx(singleShot.getTimelineLength(bpm)));
        REQUIRE(magda::test::audioEvent(preview).anchorSeconds() ==
                Catch::Approx(magda::audioEventRef(singleShot).anchorSeconds()));

        // Dragging back to where it started restores the original state
        ClipInfo backToStart = previewTick(snapshot, 6.0, 0.0);
        REQUIRE(backToStart.getTimelineStart(bpm) == Catch::Approx(10.0));
        REQUIRE(backToStart.getTimelineLength(bpm) == Catch::Approx(6.0));
        REQUIRE(magda::audioEventRef(backToStart).anchorSeconds() == Catch::Approx(3.0));
    }

    SECTION("Non-looped MIDI accumulates midiTrimOffset once, not once per tick") {
        ClipInfo snapshot;
        snapshot.setMidiContent();
        ClipOperations::setTimelinePlacement(snapshot, 0.0, 4.0, bpm);
        snapshot.midiTrimOffset = 0.0;

        ClipInfo preview = snapshot;
        for (double delta : {-0.5, -1.0, -1.5, -2.0})
            preview = previewTick(snapshot, 4.0, delta);

        // 2 seconds at 120 BPM = 4 beats, counted once
        REQUIRE(preview.getTimelineStart(bpm) == Catch::Approx(2.0));
        REQUIRE(preview.getTimelineLength(bpm) == Catch::Approx(2.0));
        REQUIRE(preview.midiTrimOffset == Catch::Approx(4.0));
    }

    SECTION("Preview matches what the commit path produces for each clip") {
        // The commit path restores the pre-drag snapshot, then resizes to
        // originalLength + lengthDelta. The preview must land in the same place.
        ClipInfo snapshot;
        snapshot.setAudioContent();
        magda::test::giveAudioEvent(snapshot, "c.wav");
        ClipOperations::setTimelinePlacement(snapshot, 4.0, 5.0, bpm);
        magda::test::audioEvent(snapshot).setAnchorSeconds(1.0);
        magda::test::audioEvent(snapshot).speedRatio = 2.0;

        const double lengthDelta = 1.5;  // expanding leftwards

        ClipInfo preview = previewTick(snapshot, 5.0, lengthDelta);

        ClipInfo committed = snapshot;  // restored before the resize command runs
        ClipOperations::resizeContainerFromLeft(committed, 5.0 + lengthDelta, bpm);
        if (auto* event = committed.primaryEvent(); event != nullptr && !committed.loopEnabled)
            event->loopStartSamples = event->sourceAnchorSamples;

        REQUIRE(preview.getTimelineStart(bpm) == Catch::Approx(committed.getTimelineStart(bpm)));
        REQUIRE(preview.getTimelineLength(bpm) == Catch::Approx(committed.getTimelineLength(bpm)));
        REQUIRE(magda::test::audioEvent(preview).anchorSeconds() ==
                Catch::Approx(magda::test::audioEvent(committed).anchorSeconds()));
        REQUIRE(magda::test::audioEvent(preview).loopStartSeconds() ==
                Catch::Approx(magda::test::audioEvent(committed).loopStartSeconds()));
    }

    SECTION("A short clip in the selection clamps instead of inverting") {
        ClipInfo shortClip;
        magda::test::giveAudioEvent(shortClip, "d.wav");
        ClipOperations::setTimelinePlacement(shortClip, 8.0, 0.5, bpm);
        magda::test::audioEvent(shortClip).setAnchorSeconds(0.0);

        // Drag right far past the short clip's own length
        ClipInfo preview = previewTick(shortClip, 0.5, -2.0);

        REQUIRE(preview.getTimelineLength(bpm) >= 0.1);
        REQUIRE(preview.getTimelineStart(bpm) < preview.getTimelineEnd(bpm));
        // End still pinned where it was
        REQUIRE(preview.getTimelineEnd(bpm) == Catch::Approx(8.5));
    }
}
