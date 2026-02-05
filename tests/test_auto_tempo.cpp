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
 * - setAutoTempo calibrates sourceBPM to projectBPM/speedRatio (no speed change at transition)
 * - getAutoTempoBeatRange produces beats that map to correct source positions via calibrated BPM
 * - Clip length is correct after enabling musical mode
 * - getEndBeats returns consistent values
 * - Round-trip: enable → disable → enable preserves behavior
 */

using namespace magda;
using Catch::Approx;

// Amen break-like source file: ~1.513s, 4 beats at ~158.6 BPM
static constexpr double AMEN_DURATION = 1.513;
static constexpr double AMEN_ORIGINAL_BPM = 158.6;
static constexpr double AMEN_SOURCE_BEATS = 4.0;
static constexpr double AMEN_FILE_DURATION =
    AMEN_SOURCE_BEATS * 60.0 / AMEN_ORIGINAL_BPM;  // ~1.513s

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
    clip.sourceBPM = AMEN_ORIGINAL_BPM;
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

    SECTION("loopLengthBeats is in project beats") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        double expectedProjectBeats = (AMEN_DURATION * PROJECT_BPM) / 60.0;
        REQUIRE(clip.loopLengthBeats == Approx(expectedProjectBeats));
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
// sourceBPM calibration — no speed change at transition
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - calibrates sourceBPM to prevent speed change", "[clip][auto-tempo]") {
    SECTION("sourceBPM becomes projectBPM when speedRatio=1.0") {
        auto clip = makeAmenClip();
        REQUIRE(clip.sourceBPM == Approx(AMEN_ORIGINAL_BPM));

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        // After calibration: sourceBPM = projectBPM / speedRatio = projectBPM
        REQUIRE(clip.sourceBPM == Approx(PROJECT_BPM));
    }

    SECTION("sourceNumBeats scaled to preserve file duration") {
        auto clip = makeAmenClip();

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        // File duration = originalNumBeats * 60 / originalBPM
        // New numBeats = calibratedBPM * fileDuration / 60
        double expectedNumBeats = PROJECT_BPM * AMEN_FILE_DURATION / 60.0;
        REQUIRE(clip.sourceNumBeats == Approx(expectedNumBeats));
    }

    SECTION("sourceBPM = projectBPM / speedRatio when speedRatio != 1.0") {
        auto clip = makeAmenClip();
        clip.speedRatio = 2.0;

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        // effectiveBPM = 69 / 2.0 = 34.5
        REQUIRE(clip.sourceBPM == Approx(PROJECT_BPM / 2.0));
    }

    SECTION("No-stretch invariant: calibrated beat range maps back to original source time") {
        auto clip = makeAmenClip();

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // These beats, when TE maps them through loopInfo.bpm = calibrated sourceBPM,
        // must map back to the original source-time positions
        double sourceStart = startBeats * 60.0 / clip.sourceBPM;
        double sourceLength = lengthBeats * 60.0 / clip.sourceBPM;

        REQUIRE(sourceStart == Approx(clip.loopStart));
        REQUIRE(sourceLength == Approx(clip.loopLength));
    }

    SECTION("TE stretch ratio is 1.0 at transition (projectBPM / sourceBPM = 1)") {
        auto clip = makeAmenClip();

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        double stretchRatio = PROJECT_BPM / clip.sourceBPM;
        REQUIRE(stretchRatio == Approx(1.0));
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
// getAutoTempoBeatRange — after calibration, source beats
// equal project beats (since sourceBPM = projectBPM)
// ─────────────────────────────────────────────────────────────

TEST_CASE("getAutoTempoBeatRange - calibrated beat range", "[clip][auto-tempo][te-sync]") {
    SECTION("After calibration, source beats equal project beats (no stretch)") {
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // With sourceBPM calibrated to projectBPM, the beat conversion
        // produces the same values as project beats
        REQUIRE(lengthBeats == Approx(clip.loopLengthBeats));
    }

    SECTION("Beat range maps to correct source-time positions") {
        auto clip = makeAmenClip();
        clip.loopEnabled = true;
        clip.loopStart = 0.3;
        clip.loopLength = 0.8;

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // Round-trip: beats → source time via calibrated BPM = original source time
        double recoveredStart = startBeats * 60.0 / clip.sourceBPM;
        double recoveredLength = lengthBeats * 60.0 / clip.sourceBPM;

        REQUIRE(recoveredStart == Approx(0.3));
        REQUIRE(recoveredLength == Approx(0.8));
    }

    SECTION("Returns {0,0} when autoTempo is off") {
        auto clip = makeAmenClip();
        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        REQUIRE(startBeats == 0.0);
        REQUIRE(lengthBeats == 0.0);
    }

    SECTION("Calibration works even when sourceBPM was initially unknown") {
        auto clip = makeAmenClip();
        clip.sourceBPM = 0.0;  // unknown before setAutoTempo
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        // sourceBPM should still be calibrated to projectBPM
        REQUIRE(clip.sourceBPM == Approx(PROJECT_BPM));

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);
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

    SECTION("Clamping shifts start when loop exceeds file with offset") {
        // offset=0.5 + loopLength=1.513 = 2.013 > file(1.513)
        // So clamping must shift the start back
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // Beat range must fit within calibrated sourceNumBeats
        REQUIRE(startBeats >= 0.0);
        REQUIRE(startBeats + lengthBeats <= clip.sourceNumBeats + 0.001);
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
// Different project BPMs — calibration ensures no stretch at
// transition regardless of project tempo
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - different project BPMs", "[clip][auto-tempo]") {
    SECTION("At 120 BPM, sourceBPM calibrated to 120") {
        auto clip = makeAmenClip();
        clip.sourceBPM = 120.0;
        clip.sourceNumBeats = 4.0;
        clip.length = 2.0;  // 4 beats at 120 BPM

        ClipOperations::setAutoTempo(clip, true, 120.0);

        REQUIRE(clip.sourceBPM == Approx(120.0));
        REQUIRE(clip.length == Approx(2.0));
        REQUIRE(clip.loopLengthBeats == Approx(4.0));

        // Stretch ratio = 120/120 = 1.0 (no stretch)
        REQUIRE(120.0 / clip.sourceBPM == Approx(1.0));
    }

    SECTION("At 60 BPM, sourceBPM calibrated to 60 — no stretch at transition") {
        auto clip = makeAmenClip();
        clip.sourceBPM = 120.0;
        clip.sourceNumBeats = 4.0;
        clip.length = 2.0;

        ClipOperations::setAutoTempo(clip, true, 60.0);

        REQUIRE(clip.sourceBPM == Approx(60.0));
        REQUIRE(clip.loopLengthBeats == Approx(2.0));  // 2.0s * 60/60 = 2.0 beats

        // Stretch ratio = 60/60 = 1.0 (no stretch at transition)
        REQUIRE(60.0 / clip.sourceBPM == Approx(1.0));

        // sourceNumBeats recalculated to preserve file duration
        // File duration = 4.0 * 60/120 = 2.0s
        // New numBeats = 60 * 2.0/60 = 2.0
        REQUIRE(clip.sourceNumBeats == Approx(2.0));
    }

    SECTION("At 200 BPM, sourceBPM calibrated to 200 — no stretch at transition") {
        auto clip = makeAmenClip();

        ClipOperations::setAutoTempo(clip, true, 200.0);

        REQUIRE(clip.sourceBPM == Approx(200.0));
        REQUIRE(200.0 / clip.sourceBPM == Approx(1.0));
    }
}

// ─────────────────────────────────────────────────────────────
// Regression: loop region wrapping past file end
//
// When the loop extends past the file duration, the beat-based
// range must be clamped to fit within sourceNumBeats.  This test
// verifies clamping still works correctly after BPM calibration.
// ─────────────────────────────────────────────────────────────

TEST_CASE("Regression: loop wrapping past file end with calibration",
          "[clip][auto-tempo][regression]") {
    // 6s file, original BPM 138, project 69
    static constexpr double FILE_DURATION = 6.0;
    static constexpr double ORIG_BPM = 138.0;
    static constexpr double ORIG_BEATS = FILE_DURATION * ORIG_BPM / 60.0;  // 13.8

    ClipInfo clip;
    clip.type = ClipType::Audio;
    clip.audioFilePath = "amen_break.wav";
    clip.startTime = 0.0;
    clip.speedRatio = 1.0;
    clip.sourceBPM = ORIG_BPM;
    clip.sourceNumBeats = ORIG_BEATS;

    // 2-bar clip at 69 BPM, with 1-bar loop starting at bar 2
    double barSeconds = 4.0 * 60.0 / PROJECT_BPM;  // ~3.478s per bar
    clip.length = 2.0 * barSeconds;
    clip.offset = barSeconds;
    clip.loopEnabled = true;
    clip.loopStart = barSeconds;
    clip.loopLength = barSeconds;

    // Precondition: loop extends past the file in source time
    REQUIRE(clip.loopStart + clip.loopLength > FILE_DURATION);

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    // After calibration: sourceBPM = 69, sourceNumBeats = 69 * 6.0/60 = 6.9
    REQUIRE(clip.sourceBPM == Approx(PROJECT_BPM));
    double calibratedNumBeats = PROJECT_BPM * FILE_DURATION / 60.0;
    REQUIRE(clip.sourceNumBeats == Approx(calibratedNumBeats));

    auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

    SECTION("Beat range fits within calibrated sourceNumBeats") {
        REQUIRE(startBeats >= 0.0);
        REQUIRE(startBeats + lengthBeats <= clip.sourceNumBeats + 0.001);
    }

    SECTION("Start is shifted back to make room for the loop") {
        // Without clamping, start would be barSeconds * 69/60 = 4.0
        double unclampedStart = barSeconds * clip.sourceBPM / 60.0;
        double unclampedEnd = unclampedStart + barSeconds * clip.sourceBPM / 60.0;
        // Verify the unclamped range would exceed sourceNumBeats
        REQUIRE(unclampedEnd > clip.sourceNumBeats);
        // Verify clamping shifted start back
        REQUIRE(startBeats < unclampedStart);
    }

    SECTION("Beat positions map back to source time correctly") {
        double recoveredStart = startBeats * 60.0 / clip.sourceBPM;
        double recoveredLength = lengthBeats * 60.0 / clip.sourceBPM;
        // The recovered region should fit within the file
        REQUIRE(recoveredStart >= 0.0);
        REQUIRE(recoveredStart + recoveredLength <= FILE_DURATION + 0.001);
    }
}
