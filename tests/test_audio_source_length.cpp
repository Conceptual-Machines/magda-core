#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipDisplayInfo.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipManager.hpp"

namespace {

void syncPlacement(magda::ClipInfo& clip, double bpm = 120.0) {
    clip.setPlacementBeats(clip.startTime * bpm / 60.0, clip.length * bpm / 60.0);
}

}  // namespace

/**
 * Tests for the ClipDisplayInfo file-extent / loop-region contract.
 *
 * The contract:
 *   - sourceFileStart / sourceFileEnd  → ALWAYS describe the source file
 *                                         on disk. The drawable range, full
 *                                         stop. Loop state never gates this.
 *   - loopRegionStartSource / Length   → The loop subset within the file.
 *                                         Drawn as overlay; never used to
 *                                         shrink the drawable range.
 *
 * The previous version of this struct overloaded one field to mean both
 * "loop region" and "drawable extent", which made the editor truncate the
 * waveform whenever the loop region was smaller than the file. The cases
 * below pin the new separation so it can't regress silently.
 */

// ============================================================================
// File extent — independent of loop state
// ============================================================================

TEST_CASE("ClipDisplayInfo - file extent always covers the full source file",
          "[clip][display][source]") {
    using namespace magda;

    SECTION("Non-loop, fileDuration known: extent = [0, fileDuration]") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 1.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/5.0);

        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(5.0));
    }

    SECTION("Loop enabled with sub-region: extent still covers the full file") {
        // The exact regression scenario: a 4 s file, loop region shrunk to
        // bars 2-4 (loopStart = 1 s, loopLength = 3 s). The file extent
        // must still be [0, 4] so the editor can draw the whole file with
        // the loop region as an overlay.
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 1.0;
        clip.loopLength = 3.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(4.0));
        REQUIRE(di.fileExtentSource() == Catch::Approx(4.0));
    }

    SECTION("Loop region shorter than file: loop fields reflect region, extent reflects file") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 0.0;
        clip.loopLength = 5.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/8.0);

        // Loop region is the user's selected subset.
        REQUIRE(di.loopRegionStartSource == Catch::Approx(0.0));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(5.0));
        REQUIRE(di.loopLengthSeconds == Catch::Approx(5.0));

        // File extent is the whole file regardless.
        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.sourceFileEnd == Catch::Approx(8.0));
    }

    SECTION("fileDuration unknown: fall back to clip-derived extent so the editor still draws") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/0.0);

        REQUIRE(di.sourceFileStart == Catch::Approx(0.0));
        REQUIRE(di.fileExtentSource() > 0.0);
    }
}

// ============================================================================
// Source ↔ timeline conversion
// ============================================================================

TEST_CASE("ClipDisplayInfo - srcToTimelineRatio drives both directions",
          "[clip][display][conversion]") {
    using namespace magda;

    SECTION("speedRatio = 1: timeline == source") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.sourceToTimeline(2.0) == Catch::Approx(2.0));
        REQUIRE(di.timelineToSource(2.0) == Catch::Approx(2.0));
        REQUIRE(di.fileExtentTimeline() == Catch::Approx(4.0));
    }

    SECTION("speedRatio = 2: timeline = source / 2") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 2.0;
        clip.loopEnabled = false;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/8.0);

        REQUIRE(di.sourceToTimeline(8.0) == Catch::Approx(4.0));
        REQUIRE(di.timelineToSource(4.0) == Catch::Approx(8.0));
        REQUIRE(di.fileExtentTimeline() == Catch::Approx(4.0));
        REQUIRE(di.fileExtentSource() == Catch::Approx(8.0));
    }
}

// ============================================================================
// Loop region — independent of file extent
// ============================================================================

TEST_CASE("ClipDisplayInfo - loop region tracks clip.loopStart / loopLength",
          "[clip][display][loop]") {
    using namespace magda;

    SECTION("loopEnabled with loopLength=0: sentinel falls back to remaining source") {
        // Older clips, freshly-toggled loops, and the session scheduler
        // treat (loopEnabled=true, loopLength=0) as "loop the whole
        // source from loopStart". The editor must mirror that — without
        // the fallback the clip would play looped in audio while the
        // overlay drew it as non-looped.
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 1.0;
        clip.loopLength = 0.0;  // sentinel

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.isLooped());
        REQUIRE(di.loopRegionStartSource == Catch::Approx(1.0));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(3.0));  // file - loopStart
    }

    SECTION("Loop disabled: loop region length is 0, isLooped() is false") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 4.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = false;
        clip.loopLength = 4.0;  // present but ignored when disabled

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE_FALSE(di.isLooped());
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(4.0));  // mirrors clip
        REQUIRE(di.loopLengthSeconds == Catch::Approx(4.0));
    }

    SECTION("Loop enabled with sub-region: loop fields match user selection") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 1.0;
        clip.loopLength = 3.0;

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.isLooped());
        REQUIRE(di.loopRegionStartSource == Catch::Approx(1.0));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(3.0));
        REQUIRE(di.loopStartPositionSeconds == Catch::Approx(1.0));
        REQUIRE(di.loopEndPositionSeconds == Catch::Approx(4.0));
    }

    SECTION("Loop region beyond file end: clamped to fit, file extent unchanged") {
        ClipInfo clip;
        clip.setAudioContent();
        clip.startTime = 0.0;
        clip.length = 16.0;
        clip.offset = 0.0;
        clip.speedRatio = 1.0;
        clip.loopEnabled = true;
        clip.loopStart = 3.0;
        clip.loopLength = 5.0;  // would extend to 8s, file only 4s

        syncPlacement(clip);
        auto di = ClipDisplayInfo::from(clip, 120.0, /*fileDuration=*/4.0);

        REQUIRE(di.loopRegionStartSource == Catch::Approx(3.0));
        REQUIRE(di.loopRegionLengthSource == Catch::Approx(1.0));  // clamped
        REQUIRE(di.sourceFileEnd == Catch::Approx(4.0));           // file extent unchanged
    }
}

// ============================================================================
// ClipManager - setClipLoopEnabled preserves clip's own loop length
// ============================================================================

TEST_CASE("ClipManager - setClipLoopEnabled preserves loopLength", "[audio][clip][loop][source]") {
    using namespace magda;

    ClipManager::getInstance().shutdown();

    SECTION("Enabling loop keeps the loop length set at creation") {
        ClipId clipId = ClipManager::getInstance().createAudioClip(1, 0.0, 4.0, "test.wav");
        auto* clip = ClipManager::getInstance().getClip(clipId);
        REQUIRE(clip != nullptr);

        clip->speedRatio = 1.0;
        REQUIRE(clip->loopLength == Catch::Approx(4.0));

        ClipManager::getInstance().setClipLoopEnabled(clipId, true, 120.0);

        REQUIRE(clip->loopLength == Catch::Approx(4.0));
        REQUIRE(clip->loopEnabled == true);
    }
}
