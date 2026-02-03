#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipDisplayInfo.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

/**
 * Tests for audioSourceLength - preserving source extent when enabling loop mode
 *
 * These tests verify:
 * - audioSourceLength is captured when enabling loop mode
 * - ClipDisplayInfo uses audioSourceLength in loop mode
 * - ClipDisplayInfo ignores audioSourceLength in non-loop mode (uses clip.length)
 * - Source extent calculations are correct for waveform editor display
 */

// ============================================================================
// ClipManager - setClipLoopEnabled preserves source extent
// ============================================================================

TEST_CASE("ClipManager - setClipLoopEnabled preserves audioSourceLength",
          "[audio][clip][loop][source]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Enabling loop captures current length as audioSourceLength") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->audioStretchFactor = 1.0;
        REQUIRE(clip->audioSourceLength == 0.0);  // Not set initially

        // Enable loop mode
        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        // audioSourceLength should now be set to clip.length / stretchFactor
        REQUIRE(clip->audioSourceLength == Catch::Approx(4.0));
        REQUIRE(clip->internalLoopEnabled == true);
    }

    SECTION("Enabling loop with stretch factor converts to source seconds") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->audioStretchFactor = 2.0;  // 2x slower, so 8s timeline = 4s source
        REQUIRE(clip->audioSourceLength == 0.0);

        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        // audioSourceLength = 8.0 / 2.0 = 4.0 source seconds
        REQUIRE(clip->audioSourceLength == Catch::Approx(4.0));
    }

    SECTION("Enabling loop does not overwrite existing audioSourceLength") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 8.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->audioStretchFactor = 1.0;
        clip->audioSourceLength = 3.0;  // User has already set this

        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        // Should NOT overwrite the user's value
        REQUIRE(clip->audioSourceLength == Catch::Approx(3.0));
    }

    SECTION("Disabling loop preserves audioSourceLength") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        clip->audioStretchFactor = 1.0;

        // Enable then disable
        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);
        REQUIRE(clip->audioSourceLength == Catch::Approx(4.0));

        ClipManager::getInstance().setClipLoopEnabled(clipId, false, 120.0);

        // audioSourceLength should still be set
        REQUIRE(clip->audioSourceLength == Catch::Approx(4.0));
    }
}

TEST_CASE("ClipManager - setAudioSourceLength", "[audio][clip][source]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("setAudioSourceLength sets value for audio clips") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        ClipManager::getInstance().setAudioSourceLength(clipId, 2.5);
        REQUIRE(clip->audioSourceLength == Catch::Approx(2.5));
    }

    SECTION("setAudioSourceLength clamps to non-negative") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);

        ClipManager::getInstance().setAudioSourceLength(clipId, -5.0);
        REQUIRE(clip->audioSourceLength == 0.0);
    }
}

// ============================================================================
// ClipDisplayInfo - source length behavior in loop vs non-loop mode
// ============================================================================

TEST_CASE("ClipDisplayInfo - sourceLength in loop mode uses audioSourceLength",
          "[clip][display][loop][source]") {
    using namespace magda;

    SECTION("Loop mode with audioSourceLength set: uses audioSourceLength") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;  // Long clip (multiple loop cycles)
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM
        clip.audioSourceLength = 3.0;   // User's selected source extent

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength should be audioSourceLength, not derived from clip.length
        REQUIRE(di.sourceLength == Catch::Approx(3.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(3.0));  // 3.0 * 1.0
    }

    SECTION("Loop mode with audioSourceLength=0: falls back to clip.length") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;
        clip.audioSourceLength = 0.0;  // Not set

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // Falls back to clip.length / stretchFactor
        REQUIRE(di.sourceLength == Catch::Approx(8.0));
    }

    SECTION("Loop mode with stretch: sourceExtentSeconds = sourceLength * stretchFactor") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 2.0;  // 2x slower
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;
        clip.audioSourceLength = 3.0;  // 3s of source audio

        auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.sourceLength == Catch::Approx(3.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(6.0));  // 3.0 * 2.0
    }
}

TEST_CASE("ClipDisplayInfo - sourceLength in non-loop mode ignores audioSourceLength",
          "[clip][display][source]") {
    using namespace magda;

    SECTION(
        "Non-loop mode: sourceLength derived from clip.length regardless of audioSourceLength") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = false;
        clip.audioSourceLength = 10.0;  // This should be ignored

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // In non-loop mode, sourceLength = clip.length / stretchFactor
        REQUIRE(di.sourceLength == Catch::Approx(4.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(4.0));
    }

    SECTION("Non-loop mode with stretch: sourceLength = clip.length / stretchFactor") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 2.0;
        clip.internalLoopEnabled = false;
        clip.audioSourceLength = 10.0;  // Ignored

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength = 8.0 / 2.0 = 4.0
        REQUIRE(di.sourceLength == Catch::Approx(4.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(8.0));  // 4.0 * 2.0
    }
}

TEST_CASE("ClipDisplayInfo - sourceFileEnd uses sourceLength in non-loop mode",
          "[clip][display][source]") {
    using namespace magda;

    SECTION("Non-loop mode: sourceFileEnd = audioOffset + sourceLength") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.audioOffset = 1.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = false;
        clip.audioSourceLength = 0.0;

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength = 4.0 / 1.0 = 4.0
        // sourceFileEnd = 1.0 + 4.0 = 5.0
        REQUIRE(di.sourceFileStart == Catch::Approx(1.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(5.0));
    }

    SECTION("Non-loop mode with stretch: sourceFileEnd accounts for stretch") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 8.0;  // 8s on timeline
        clip.audioOffset = 0.5;
        clip.audioStretchFactor = 2.0;  // 2x slower
        clip.internalLoopEnabled = false;

        auto di = ClipDisplayInfo::from(clip, 120.0);

        // sourceLength = 8.0 / 2.0 = 4.0
        // sourceFileEnd = 0.5 + 4.0 = 4.5
        REQUIRE(di.sourceFileStart == Catch::Approx(0.5));
        REQUIRE(di.sourceFileEnd == Catch::Approx(4.5));
    }
}

// ============================================================================
// Integration: source extent vs loop end for waveform editor
// ============================================================================

TEST_CASE("ClipDisplayInfo - sourceExtentSeconds > loopEndPositionSeconds for waveform editor",
          "[clip][display][loop][waveform]") {
    using namespace magda;

    SECTION("Source extent larger than loop allows waveform editor to show remaining audio") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;  // Long clip
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 4 beats = 2s at 120 BPM
        clip.audioSourceLength = 5.0;   // 5s of source (more than 2s loop)

        auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.loopEndPositionSeconds == Catch::Approx(2.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(5.0));

        // This is the key condition for drawing remaining audio in waveform editor
        REQUIRE(di.sourceExtentSeconds > di.loopEndPositionSeconds);
    }

    SECTION("Source extent equals loop end - no remaining audio to show") {
        ClipInfo clip;
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.audioOffset = 0.0;
        clip.audioStretchFactor = 1.0;
        clip.internalLoopEnabled = true;
        clip.internalLoopOffset = 0.0;
        clip.internalLoopLength = 4.0;  // 2s at 120 BPM
        clip.audioSourceLength = 2.0;   // Exactly matches loop

        auto di = ClipDisplayInfo::from(clip, 120.0);

        REQUIRE(di.loopEndPositionSeconds == Catch::Approx(2.0));
        REQUIRE(di.sourceExtentSeconds == Catch::Approx(2.0));
        REQUIRE_FALSE(di.sourceExtentSeconds > di.loopEndPositionSeconds);
    }
}
