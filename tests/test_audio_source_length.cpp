#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipDisplayInfo.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

/**
 * Tests for source region - preserving source extent when enabling loop mode
 *
 * TE-aligned model:
 * - offset: start position in source file (seconds)
 * - loopStart: where loop region starts in source file (seconds)
 * - loopLength: length of loop region in source file (seconds)
 * - speedRatio: playback speed ratio (1.0 = normal, 2.0 = 2x faster)
 *
 * These tests verify:
 * - loopLength is captured when enabling loop mode
 * - ClipDisplayInfo uses loopLength in loop mode
 * - ClipDisplayInfo derives sourceLength in non-loop mode (from clip.length)
 * - Source extent calculations are correct for waveform editor display
 */

// ============================================================================
// ClipManager - setClipLoopEnabled preserves source extent
// ============================================================================

TEST_CASE("ClipManager - setClipLoopEnabled preserves source extent",
          "[audio][clip][loop][source]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Enabling loop captures current length as source extent") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->speedRatio = 1.0;
        // loopLength is set at creation: length * speedRatio = 4.0
        REQUIRE(clip->loopLength == Catch::Approx(4.0));

        // Enable loop mode — loopLength already set, should not change
        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        REQUIRE(clip->loopLength == Catch::Approx(4.0));
        REQUIRE(clip->loopEnabled == true);
    }

    SECTION("Enabling loop with speed ratio converts to source seconds") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        // loopLength set at creation: 8.0 * 1.0 = 8.0
        REQUIRE(clip->loopLength == Catch::Approx(8.0));

        // Manually change speed ratio (simulating stretch)
        clip->speedRatio = 2.0;  // 2x faster, so 8s timeline = 16s source
        // Reset loopLength to 0 to test that setClipLoopEnabled recalculates
        clip->loopLength = 0.0;

        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        // loopLength = 8.0 * 2.0 = 16.0 source seconds
        REQUIRE(clip->loopLength == Catch::Approx(16.0));
    }

    SECTION("Enabling loop does not overwrite existing loopLength") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->speedRatio = 1.0;
        clip->loopLength = 3.0;  // User has already set this

        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        // Should NOT overwrite the user's value
        REQUIRE(clip->loopLength == Catch::Approx(3.0));
    }

    SECTION("Disabling loop preserves loopLength") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->speedRatio = 1.0;

        // Enable then disable
        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);
        REQUIRE(clip->loopLength == Catch::Approx(4.0));

        ClipManager::getInstance().setClipLoopEnabled(clipId, false, 120.0);

        // loopLength should still be set
        REQUIRE(clip->loopLength == Catch::Approx(4.0));
    }
}

TEST_CASE("ClipManager - setLoopLength", "[audio][clip][source]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("setLoopLength sets value for audio clips") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        ClipManager::getInstance().setLoopLength(clipId, 2.5);
        REQUIRE(clip->loopLength == Catch::Approx(2.5));
    }

    SECTION("setLoopLength clamps to non-negative") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        ClipManager::getInstance().setLoopLength(clipId, -5.0);
        REQUIRE(clip->loopLength == 0.0);
    }
}

// ============================================================================
// ClipDisplayInfo - source length behavior in loop vs non-loop mode
// ============================================================================

TEST_CASE("ClipDisplayInfo - sourceLength in loop mode uses loopLength",
          "[clip][display][loop][source]") {
    using namespace magda;

    SECTION("Loop mode with loopLength set: uses loopLength directly") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;  // Long clip (multiple loop cycles)
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;

        clip.loopStart = 0.0;
        clip.loopLength = 3.0;  // User's selected source extent

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength should be loopLength
        REQUIRE(di.sourceLength == Catch::Approx(3.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(3.0));  // 3.0 * 1.0
    }

    SECTION("Loop mode with loopLength=0: falls back to clip.length * speedRatio") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;

        clip.loopStart = 0.0;
        clip.loopLength = 0.0;  // Not set

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // Falls back to clip.length * speedRatio
        REQUIRE(di.sourceLength == Catch::Approx(8.0));
    }

    SECTION("Loop mode with speed ratio: sourceExtentSeconds = sourceLength / speedRatio") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.offset = 0.0;
        clip.speedRatio = 2.0;  // 2x faster
        clip.loopEnabled = true;

        clip.loopStart = 0.0;
        clip.loopLength = 3.0;  // 3s of source audio

        auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.sourceLength == Catch::Approx(3.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(1.5));  // 3.0 / 2.0
    }
}

TEST_CASE("ClipDisplayInfo - sourceLength in non-loop mode derives from clip.length",
          "[clip][display][source]") {
    using namespace magda;

    SECTION("Non-loop mode: sourceLength = clip.length * speedRatio") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;
        clip.loopLength = 10.0;  // This should be ignored in non-loop mode

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // In non-loop mode, sourceLength derives from clip.length * speedRatio
        REQUIRE(di.sourceLength == Catch::Approx(4.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(4.0));
    }

    SECTION("Non-loop mode with speed ratio: sourceLength = clip.length * speedRatio") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 2.0;  // 2x faster
        clip.loopEnabled = false;
        clip.loopLength = 0.0;

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength = 4.0 * 2.0 = 8.0
        REQUIRE(di.sourceLength == Catch::Approx(8.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(4.0));  // 8.0 / 2.0
    }
}

TEST_CASE("ClipDisplayInfo - sourceFileEnd in non-loop mode", "[clip][display][source]") {
    using namespace magda;

    SECTION("Non-loop mode: sourceFileEnd = offset + sourceLength") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 1.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;
        clip.loopLength = 0.0;

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength = 4.0 * 1.0 = 4.0
        // sourceFileEnd = 1.0 + 4.0 = 5.0
        REQUIRE(di.sourceFileStart == Catch::Approx(1.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(5.0));
    }

    SECTION("Non-loop mode with speed ratio: sourceFileEnd accounts for speed") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;  // 8s on timeline
        clip.offset = 0.5;
        clip.speedRatio = 2.0;  // 2x faster
        clip.loopEnabled = false;
        clip.loopLength = 0.0;

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // At 2x speed, 8s on timeline consumes 16s of source (timelineToSource = length *
        // speedRatio) sourceFileEnd = offset + sourceLength = 0.5 + 16.0 = 16.5
        REQUIRE(di.sourceFileStart == Catch::Approx(0.5));
        REQUIRE(di.sourceFileEnd == Catch::Approx(16.5));
    }
}

// ============================================================================
// Integration: source extent vs loop end for waveform editor
// ============================================================================

TEST_CASE("ClipDisplayInfo - sourceExtentSeconds and loopEndPositionSeconds for waveform editor",
          "[clip][display][loop][waveform]") {
    using namespace magda;

    SECTION("Source extent controls waveform editor visible region") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;  // Long clip
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;

        clip.loopStart = 0.0;
        clip.loopLength = 5.0;  // 5s of source

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // Loop length is the source extent
        REQUIRE(di.loopLengthSeconds == Catch::Approx(5.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(5.0));
    }

    SECTION("Source extent equals loop - loopEndPositionSeconds matches") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;

        clip.loopStart = 0.0;
        clip.loopLength = 2.0;  // 2s loop

        auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.loopLengthSeconds == Catch::Approx(2.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(2.0));
    }
}
