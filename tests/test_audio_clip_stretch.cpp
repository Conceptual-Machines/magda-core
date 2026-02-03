#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for audio clip time-stretching and trimming operations
 *
 * These tests verify:
 * - Audio stretch factor clamping and behavior
 * - Trim operations maintain absolute timeline positions
 * - Stretch operations maintain file time window
 * - Left-edge resize properly trims audio file offset
 */

TEST_CASE("Audio clip - Stretch factor basics", "[audio][clip][stretch]") {
    using namespace magda;

    SECTION("Default stretch factor is 1.0") {
        ClipInfo clip;
        clip.audioFilePath = "test.wav";
        clip.length = 4.0;
        clip.speedRatio = 1.0;

        // File window equals length when stretch factor is 1.0
        double fileWindow = clip.length / clip.speedRatio;
        REQUIRE(fileWindow == 4.0);
    }

    SECTION("Stretch factor affects file time window") {
        ClipInfo clip;
        clip.audioFilePath = "test.wav";
        clip.offset = 0.0;
        clip.length = 8.0;
        clip.speedRatio = 2.0;  // 2x slower

        // File window is half the length when stretched 2x
        double fileWindow = clip.length / clip.speedRatio;
        REQUIRE(fileWindow == 4.0);

        // Reading from file offset 0-4, displaying as 0-8 seconds
    }

    SECTION("Stretch factor 0.5 = 2x faster") {
        ClipInfo clip;
        clip.audioFilePath = "test.wav";
        clip.offset = 0.0;
        clip.length = 2.0;
        clip.speedRatio = 0.5;  // 2x faster

        // File window is double the length when compressed 2x
        double fileWindow = clip.length / clip.speedRatio;
        REQUIRE(fileWindow == 4.0);

        // Reading from file offset 0-4, displaying as 0-2 seconds
    }
}

TEST_CASE("ClipManager - setSpeedRatio clamping", "[audio][clip][stretch]") {
    using namespace magda;

    // Reset and setup
    ClipManager::getInstance().shutdown();

    SECTION("Stretch factor clamped to [0.25, 4.0] range") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        REQUIRE(clipId != INVALID_CLIP_ID);

        const auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);
        REQUIRE(clip->audioFilePath == "test.wav");

        // Test minimum clamp
        ClipManager::getInstance().setSpeedRatio(clipId, 0.1);
        REQUIRE(clip->speedRatio == 0.25);

        // Test maximum clamp
        ClipManager::getInstance().setSpeedRatio(clipId, 10.0);
        REQUIRE(clip->speedRatio == 4.0);

        // Test valid range
        ClipManager::getInstance().setSpeedRatio(clipId, 1.5);
        REQUIRE(clip->speedRatio == 1.5);

        ClipManager::getInstance().setSpeedRatio(clipId, 0.5);
        REQUIRE(clip->speedRatio == 0.5);
    }
}

TEST_CASE("Audio Clip - Left edge resize trims file offset", "[audio][clip][trim]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Trim from left advances file offset (audio at clip start)") {
        // Create audio clip: starts at 0, length 4.0
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->offset = 0.0;
        clip->speedRatio = 1.0;

        // Trim from left by 1.0 seconds
        ClipManager::getInstance().resizeClip(clipId, 3.0, true);

        // Clip moved right by 1.0 second
        REQUIRE(clip->startTime == 1.0);
        REQUIRE(clip->length == 3.0);

        // Audio offset advanced by 1.0 second
        REQUIRE(clip->offset == Catch::Approx(1.0));
    }

    SECTION("Trim with stretch factor converts to file time") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->offset = 0.0;
        clip->speedRatio = 2.0;  // 2x slower, file window = 4.0

        // Trim from left by 2.0 timeline seconds
        ClipManager::getInstance().resizeClip(clipId, 6.0, true);

        REQUIRE(clip->startTime == 2.0);
        REQUIRE(clip->length == 6.0);

        // File trim amount = 2.0 / 2.0 = 1.0 file seconds
        REQUIRE(clip->offset == Catch::Approx(1.0));
    }
}

TEST_CASE("Audio Clip - Right edge resize doesn't change offset", "[audio][clip][resize]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Right edge resize only changes length") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->offset = 1.0;

        // Resize from right edge
        ClipManager::getInstance().resizeClip(clipId, 6.0, false);

        REQUIRE(clip->startTime == 0.0);
        REQUIRE(clip->length == 6.0);

        // Audio offset unchanged
        REQUIRE(clip->offset == 1.0);
    }
}

TEST_CASE("Audio Clip - Stretch maintains file window", "[audio][clip][stretch]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Stretching by 2x doubles length but file window stays same") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->offset = 0.0;
        clip->speedRatio = 1.0;

        double originalFileWindow = clip->length / clip->speedRatio;
        REQUIRE(originalFileWindow == 4.0);

        // Stretch 2x: length becomes 8, stretch factor becomes 2
        clip->length = 8.0;
        ClipManager::getInstance().setSpeedRatio(clipId, 2.0);

        double newFileWindow = clip->length / clip->speedRatio;
        REQUIRE(newFileWindow == Catch::Approx(originalFileWindow));
    }

    SECTION("Compressing by 0.5x halves length but file window stays same") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->offset = 1.0;
        clip->speedRatio = 1.0;

        double originalFileWindow = clip->length / clip->speedRatio;
        REQUIRE(originalFileWindow == 4.0);

        // Compress 2x: length becomes 2, stretch factor becomes 0.5
        clip->length = 2.0;
        ClipManager::getInstance().setSpeedRatio(clipId, 0.5);

        double newFileWindow = clip->length / clip->speedRatio;
        REQUIRE(newFileWindow == Catch::Approx(originalFileWindow));

        // File offset unchanged
        REQUIRE(clip->offset == 1.0);
    }
}

TEST_CASE("Audio Clip - Real-world scenario: Amen break trim", "[audio][clip][integration]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Trim amen break from left preserves timeline positions") {
        // Amen break: ~4.5 bars at given BPM = 9 seconds
        constexpr double kBPM = 120.0;
        constexpr double kSecondsPerBeat = 60.0 / kBPM;  // 0.5s at 120 BPM
        juce::ignoreUnused(kSecondsPerBeat);

        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 9.0, "amen.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->offset = 0.0;
        clip->speedRatio = 1.0;

        // Trim from left by 1.0 second (to bar 1.3, where first snare is)
        ClipManager::getInstance().resizeClip(clipId, 8.0, true);

        // Clip now starts at 1.0s
        REQUIRE(clip->startTime == 1.0);
        REQUIRE(clip->length == 8.0);

        // Audio offset advanced to 1.0s (skipping first bar)
        REQUIRE(clip->offset == Catch::Approx(1.0));
    }

    SECTION("Trim stretched amen break converts to file time") {
        // Amen break stretched 2x slower: 18 seconds timeline duration
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 18.0, "amen.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->offset = 0.0;
        clip->speedRatio = 2.0;  // 2x slower, file window = 9.0s

        // Trim from left by 2.0 timeline seconds (to first snare)
        ClipManager::getInstance().resizeClip(clipId, 16.0, true);

        REQUIRE(clip->startTime == 2.0);
        REQUIRE(clip->length == 16.0);

        // File trim amount = 2.0 / 2.0 = 1.0 file seconds
        REQUIRE(clip->offset == Catch::Approx(1.0));
    }
}

TEST_CASE("Audio Clip - Edge cases", "[audio][clip][edge]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Minimum clip length enforced") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // Try to resize to very small length
        ClipManager::getInstance().resizeClip(clipId, 0.01, false);

        // Clamped to minimum 0.1
        REQUIRE(clip->length == Catch::Approx(0.1));
    }

    SECTION("Trim to zero start time") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 1.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // Resize from left past zero
        ClipManager::getInstance().resizeClip(clipId, 6.0, true);

        // Start time clamped to zero
        REQUIRE(clip->startTime == 0.0);
        REQUIRE(clip->length == 6.0);
    }
}

TEST_CASE("ClipOperations - stretchAudioFromLeft right edge anchoring",
          "[audio][clip][stretch][regression]") {
    using namespace magda;

    SECTION("Multiple stretch events maintain fixed right edge") {
        ClipInfo clip;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.offset = 0.0;
        clip.startTime = 10.0;
        clip.length = 5.0;
        clip.speedRatio = 1.0;

        // Calculate expected right edge (should never change)
        double expectedRightEdge = 10.0 + 5.0;  // 15.0
        REQUIRE(expectedRightEdge == 15.0);

        // Capture original values at "mouseDown"
        double originalLength = clip.length;
        double originalStretchFactor = clip.speedRatio;

        // Simulate drag event 1: stretch to 6.0 seconds
        ClipOperations::stretchAudioFromLeft(clip, 6.0, originalLength, originalStretchFactor);

        double rightEdge1 = clip.startTime + clip.length;
        REQUIRE(rightEdge1 == Catch::Approx(expectedRightEdge));
        REQUIRE(clip.startTime == Catch::Approx(9.0));  // 15.0 - 6.0
        REQUIRE(clip.length == Catch::Approx(6.0));
        REQUIRE(clip.speedRatio == Catch::Approx(1.2));  // 6.0 / 5.0

        // Simulate drag event 2: stretch to 7.0 seconds (more stretching)
        ClipOperations::stretchAudioFromLeft(clip, 7.0, originalLength, originalStretchFactor);

        double rightEdge2 = clip.startTime + clip.length;
        REQUIRE(rightEdge2 == Catch::Approx(expectedRightEdge));  // Still 15.0!
        REQUIRE(clip.startTime == Catch::Approx(8.0));            // 15.0 - 7.0
        REQUIRE(clip.length == Catch::Approx(7.0));
        REQUIRE(clip.speedRatio == Catch::Approx(1.4));  // 7.0 / 5.0

        // Simulate drag event 3: compress to 4.0 seconds (user dragged right)
        ClipOperations::stretchAudioFromLeft(clip, 4.0, originalLength, originalStretchFactor);

        double rightEdge3 = clip.startTime + clip.length;
        REQUIRE(rightEdge3 == Catch::Approx(expectedRightEdge));  // Still 15.0!
        REQUIRE(clip.startTime == Catch::Approx(11.0));           // 15.0 - 4.0
        REQUIRE(clip.length == Catch::Approx(4.0));
        REQUIRE(clip.speedRatio == Catch::Approx(0.8));  // 4.0 / 5.0

        // Simulate drag event 4: back to original length
        ClipOperations::stretchAudioFromLeft(clip, 5.0, originalLength, originalStretchFactor);

        double rightEdge4 = clip.startTime + clip.length;
        REQUIRE(rightEdge4 == Catch::Approx(expectedRightEdge));  // Still 15.0!
        REQUIRE(clip.startTime == Catch::Approx(10.0));           // Back to original
        REQUIRE(clip.length == Catch::Approx(originalLength));    // Back to 5.0
        REQUIRE(clip.speedRatio == Catch::Approx(1.0));           // Back to 1.0
    }

    SECTION("Stretch factor clamping doesn't break right edge anchoring") {
        ClipInfo clip;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.offset = 0.0;
        clip.startTime = 5.0;
        clip.length = 2.0;
        clip.speedRatio = 1.0;

        double expectedRightEdge = 5.0 + 2.0;  // 7.0
        double originalLength = clip.length;
        double originalStretchFactor = clip.speedRatio;

        // Try to stretch to 10.0 (5.0x ratio) — clamped to 4.0x max
        ClipOperations::stretchAudioFromLeft(clip, 10.0, originalLength, originalStretchFactor);

        // Stretch factor clamped to 4.0, so actual length = 2.0 * 4.0 = 8.0
        REQUIRE(clip.speedRatio == Catch::Approx(4.0));
        REQUIRE(clip.length == Catch::Approx(8.0));

        // Right edge maintained
        double rightEdge = clip.startTime + clip.length;
        REQUIRE(rightEdge == Catch::Approx(expectedRightEdge));
    }

    SECTION("Stretch with pre-stretched audio maintains correct calculations") {
        ClipInfo clip;
        clip.type = ClipType::Audio;
        clip.audioFilePath = "test.wav";
        clip.offset = 0.0;
        clip.startTime = 20.0;
        clip.length = 10.0;
        clip.speedRatio = 2.0;  // Already stretched 2x

        double expectedRightEdge = 20.0 + 10.0;  // 30.0
        double originalLength = clip.length;
        double originalStretchFactor = clip.speedRatio;

        // Stretch from 10.0 to 15.0 (1.5x stretch on top of existing 2.0x)
        ClipOperations::stretchAudioFromLeft(clip, 15.0, originalLength, originalStretchFactor);

        // New stretch factor: 2.0 * (15.0 / 10.0) = 3.0
        REQUIRE(clip.speedRatio == Catch::Approx(3.0));
        REQUIRE(clip.length == Catch::Approx(15.0));

        // Right edge still anchored
        double rightEdge = clip.startTime + clip.length;
        REQUIRE(rightEdge == Catch::Approx(expectedRightEdge));
        REQUIRE(clip.startTime == Catch::Approx(15.0));  // 30.0 - 15.0
    }
}
