#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for auto-tempo (musical mode) operations
 *
 * These tests verify:
 * - setSourceMetadata only populates unset fields
 * - setAutoTempo stores beat values in project beats (not source beats)
 * - getAutoTempoBeatRange converts to source beats for TE sync
 * - Clip length is correct after enabling musical mode
 * - getEndBeats returns consistent values
 * - Round-trip: enable → disable → enable preserves behavior
 */

using namespace magda;
using Catch::Approx;

// Amen break-like source file: ~1.513s, 4 beats at ~158.6 BPM
static constexpr double AMEN_DURATION = 1.513;
static constexpr double AMEN_SOURCE_BPM = 158.6;
static constexpr double AMEN_SOURCE_BEATS = 4.0;

// Project tempo
static constexpr double PROJECT_BPM = 69.0;

static ClipInfo makeAmenClip(double startTime = 0.0) {
    ClipInfo clip;
    clip.type = ClipType::Audio;
    clip.audioFilePath = "amen_break.wav";
    clip.startTime = startTime;
    clip.length = AMEN_DURATION;  // original duration before stretching
    clip.offset = 0.0;
    clip.speedRatio = 1.0;
    clip.sourceBPM = AMEN_SOURCE_BPM;
    clip.sourceNumBeats = AMEN_SOURCE_BEATS;
    return clip;
}

// ─────────────────────────────────────────────────────────────
// ClipInfo::setSourceMetadata
// ─────────────────────────────────────────────────────────────

TEST_CASE("ClipInfo::setSourceMetadata - populates unset fields", "[clip][auto-tempo][metadata]") {
    ClipInfo clip;

    SECTION("Sets both fields when unset") {
        clip.setSourceMetadata(4.0, 120.0);
        REQUIRE(clip.sourceNumBeats == 4.0);
        REQUIRE(clip.sourceBPM == 120.0);
    }

    SECTION("Does not overwrite existing values") {
        clip.sourceNumBeats = 8.0;
        clip.sourceBPM = 140.0;
        clip.setSourceMetadata(4.0, 120.0);
        REQUIRE(clip.sourceNumBeats == 8.0);
        REQUIRE(clip.sourceBPM == 140.0);
    }

    SECTION("Ignores zero/negative input") {
        clip.setSourceMetadata(0.0, -5.0);
        REQUIRE(clip.sourceNumBeats == 0.0);
        REQUIRE(clip.sourceBPM == 0.0);
    }

    SECTION("Sets one field independently of the other") {
        clip.sourceBPM = 140.0;  // already set
        clip.setSourceMetadata(4.0, 120.0);
        REQUIRE(clip.sourceNumBeats == 4.0);  // was unset, gets populated
        REQUIRE(clip.sourceBPM == 140.0);     // was set, not overwritten
    }
}

// ─────────────────────────────────────────────────────────────
// ClipOperations::setAutoTempo — model stores PROJECT beats
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - stores project beats in model", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();

    SECTION("loopLengthBeats is in project beats, not source beats") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        // clip.length should be set from project beats
        double expectedProjectBeats = (AMEN_DURATION * PROJECT_BPM) / 60.0;
        REQUIRE(clip.loopLengthBeats == Approx(expectedProjectBeats));

        // Verify it's NOT in source beats
        double sourceBeats = AMEN_DURATION * AMEN_SOURCE_BPM / 60.0;
        REQUIRE(clip.loopLengthBeats != Approx(sourceBeats));
    }

    SECTION("clip.length stays consistent with loopLengthBeats at project BPM") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        // setLengthFromBeats(loopLengthBeats, bpm) should NOT change length
        // since loopLengthBeats was derived from length at the same bpm
        REQUIRE(clip.length == Approx(AMEN_DURATION));
    }

    SECTION("startBeats is in project beats") {
        clip.startTime = 3.478;  // exactly 4 beats at 69 BPM
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        double expectedStartBeats = (3.478 * PROJECT_BPM) / 60.0;
        REQUIRE(clip.startBeats == Approx(expectedStartBeats));
    }

    SECTION("speedRatio forced to 1.0") {
        clip.speedRatio = 2.0;
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.speedRatio == 1.0);
    }

    SECTION("looping gets enabled if not already") {
        REQUIRE_FALSE(clip.loopEnabled);
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.loopEnabled);
    }
}

// ─────────────────────────────────────────────────────────────
// getEndBeats — must not mix project + source beats
// ─────────────────────────────────────────────────────────────

TEST_CASE("getEndBeats - consistent units in auto-tempo mode", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    clip.startTime = 0.0;

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    SECTION("getEndBeats = getStartBeats + length in project beats") {
        double startBeats = clip.getStartBeats(PROJECT_BPM);
        double endBeats = clip.getEndBeats(PROJECT_BPM);
        double lengthBeats = (clip.length * PROJECT_BPM) / 60.0;

        REQUIRE(endBeats == Approx(startBeats + lengthBeats));
    }

    SECTION("getEndBeats matches startBeats + loopLengthBeats") {
        // Since both are in project beats, simple addition should work
        REQUIRE(clip.getEndBeats(PROJECT_BPM) == Approx(clip.startBeats + clip.loopLengthBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// getAutoTempoBeatRange — converts to SOURCE beats for TE
// ─────────────────────────────────────────────────────────────

TEST_CASE("getAutoTempoBeatRange - returns source beats for TE", "[clip][auto-tempo][te-sync]") {
    SECTION("With known sourceBPM, converts from source-time to source beats") {
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // loopLength is in source-time seconds; source beats = loopLength * sourceBPM / 60
        double expectedSourceBeats = clip.loopLength * AMEN_SOURCE_BPM / 60.0;
        REQUIRE(lengthBeats == Approx(expectedSourceBeats));

        // Should match the source file's actual beat count
        REQUIRE(lengthBeats == Approx(AMEN_SOURCE_BEATS).margin(0.1));
    }

    SECTION("Source beats differ from project beats when BPMs differ") {
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // Model stores project beats; TE sync should produce source beats
        REQUIRE(lengthBeats != Approx(clip.loopLengthBeats));
    }

    SECTION("Returns {0,0} when autoTempo is off") {
        auto clip = makeAmenClip();
        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        REQUIRE(startBeats == 0.0);
        REQUIRE(lengthBeats == 0.0);
    }

    SECTION("Fallback to project beats when sourceBPM is unknown") {
        auto clip = makeAmenClip();
        clip.sourceBPM = 0.0;  // unknown
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // Should fall back to the model's project-beat values
        REQUIRE(lengthBeats == Approx(clip.loopLengthBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// setAutoTempo with offset — preserves loop region
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - with offset preserves loop start", "[clip][auto-tempo][offset]") {
    auto clip = makeAmenClip();
    clip.offset = 0.5;  // start reading 0.5s into the source file

    SECTION("loopStart set to offset when loop was not enabled") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.loopStart == Approx(0.5));
    }

    SECTION("loopStartBeats in project beats corresponds to loopStart") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        double expectedStartBeats = (0.5 * PROJECT_BPM) / 60.0;
        REQUIRE(clip.loopStartBeats == Approx(expectedStartBeats));
    }

    SECTION("getAutoTempoBeatRange converts loopStart to source beats") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        double expectedSourceStartBeats = 0.5 * AMEN_SOURCE_BPM / 60.0;
        REQUIRE(startBeats == Approx(expectedSourceStartBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// setAutoTempo — existing loop preserved
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - respects existing loop region", "[clip][auto-tempo][loop]") {
    auto clip = makeAmenClip();
    clip.loopEnabled = true;
    clip.loopStart = 0.3;
    clip.loopLength = 0.8;

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    SECTION("Does not overwrite existing loopStart/loopLength") {
        REQUIRE(clip.loopStart == Approx(0.3));
        REQUIRE(clip.loopLength == Approx(0.8));
    }

    SECTION("Derives loopLengthBeats from clip length at project BPM") {
        double expectedBeats = (clip.length * PROJECT_BPM) / 60.0;
        REQUIRE(clip.loopLengthBeats == Approx(expectedBeats));
    }

    SECTION("getAutoTempoBeatRange uses loopLength in source-time") {
        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        double expectedSourceBeats = 0.8 * AMEN_SOURCE_BPM / 60.0;
        REQUIRE(lengthBeats == Approx(expectedSourceBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// Round-trip: enable → disable → enable
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - disable clears beat values", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    // Verify beat values were set
    REQUIRE(clip.loopLengthBeats > 0.0);
    REQUIRE(clip.startBeats >= 0.0);

    ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);

    SECTION("Beat values are cleared") {
        REQUIRE(clip.startBeats == -1.0);
        REQUIRE(clip.loopStartBeats == 0.0);
        REQUIRE(clip.loopLengthBeats == 0.0);
    }

    SECTION("autoTempo is false") {
        REQUIRE_FALSE(clip.autoTempo);
    }
}

TEST_CASE("setAutoTempo - no-op when already in target state", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();

    SECTION("Enable when already enabled is no-op") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        double savedLength = clip.length;
        double savedBeats = clip.loopLengthBeats;

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        REQUIRE(clip.length == Approx(savedLength));
        REQUIRE(clip.loopLengthBeats == Approx(savedBeats));
    }

    SECTION("Disable when already disabled is no-op") {
        REQUIRE_FALSE(clip.autoTempo);
        ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);
        REQUIRE_FALSE(clip.autoTempo);
    }
}

// ─────────────────────────────────────────────────────────────
// Different project BPMs — model should scale correctly
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - different project BPMs", "[clip][auto-tempo]") {
    SECTION("At 120 BPM, clip length matches source duration (no stretch)") {
        auto clip = makeAmenClip();
        clip.sourceBPM = 120.0;
        clip.sourceNumBeats = 4.0;
        clip.length = 2.0;  // 4 beats at 120 BPM

        ClipOperations::setAutoTempo(clip, true, 120.0);

        // Project BPM == source BPM → no stretching → length unchanged
        REQUIRE(clip.length == Approx(2.0));
        REQUIRE(clip.loopLengthBeats == Approx(4.0));

        auto [srcStart, srcLen] = ClipOperations::getAutoTempoBeatRange(clip, 120.0);
        REQUIRE(srcLen == Approx(4.0));  // source beats = project beats when BPMs match
    }

    SECTION("At half source BPM, TE source beats are double the project beats") {
        auto clip = makeAmenClip();
        clip.sourceBPM = 120.0;
        clip.sourceNumBeats = 4.0;
        clip.length = 2.0;
        double halfBpm = 60.0;

        ClipOperations::setAutoTempo(clip, true, halfBpm);

        // Project beats at 60 BPM: 2.0 * 60/60 = 2.0 project beats
        REQUIRE(clip.loopLengthBeats == Approx(2.0));

        // But TE source beats from loopLength: 2.0s * 120/60 = 4.0 source beats
        auto [srcStart, srcLen] = ClipOperations::getAutoTempoBeatRange(clip, halfBpm);
        REQUIRE(srcLen == Approx(4.0));
    }
}
