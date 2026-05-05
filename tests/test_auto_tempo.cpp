#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipOperations.hpp"

/**
 * Tests for auto-tempo (musical mode) operations
 *
 * These tests verify:
 * - setSourceMetadata only populates unset fields
 * - setAutoTempo uses source beats when detected BPM differs from project BPM
 * - setAutoTempo calibrates source interpretation BPM when it matches project BPM
 * - getAutoTempoBeatRange produces correct source beats for TE
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
// static constexpr double AMEN_FILE_DURATION =
//     AMEN_SOURCE_BEATS * 60.0 / AMEN_ORIGINAL_BPM;  // ~1.513s

// Project tempo
static constexpr double PROJECT_BPM = 69.0;

static ClipInfo makeAmenClip(double startTime = 0.0) {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "amen_break.wav";
    clip.startTime = startTime;
    clip.length = AMEN_DURATION;  // original duration before stretching
    clip.offset = 0.0;
    clip.speedRatio = 1.0;
    clip.audio().interpretation.bpm = AMEN_ORIGINAL_BPM;
    clip.audio().interpretation.totalBeats = AMEN_SOURCE_BEATS;
    return clip;
}

// Helper: make a clip where source interpretation BPM matches project BPM
static ClipInfo makeCalibratedClip(double projectBPM = 120.0) {
    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "sample.wav";
    clip.startTime = 0.0;
    clip.length = 2.0;
    clip.offset = 0.0;
    clip.speedRatio = 1.0;
    clip.audio().interpretation.bpm = projectBPM;  // matches project → calibration applies
    clip.audio().interpretation.totalBeats = 4.0;
    return clip;
}

// ─────────────────────────────────────────────────────────────
// ClipInfo::setSourceMetadata
// ─────────────────────────────────────────────────────────────

TEST_CASE("ClipInfo::setSourceMetadata - populates unset fields", "[clip][auto-tempo][metadata]") {
    ClipInfo clip;
    clip.setAudioContent();

    SECTION("Sets both fields when unset") {
        clip.setSourceMetadata(4.0, 120.0);
        REQUIRE(clip.audio().interpretation.totalBeats == 4.0);
        REQUIRE(clip.audio().interpretation.bpm == 120.0);
    }

    SECTION("Does not overwrite existing values") {
        clip.audio().interpretation.totalBeats = 8.0;
        clip.audio().interpretation.bpm = 140.0;
        clip.setSourceMetadata(4.0, 120.0);
        REQUIRE(clip.audio().interpretation.totalBeats == 8.0);
        REQUIRE(clip.audio().interpretation.bpm == 140.0);
    }

    SECTION("Ignores zero/negative input") {
        clip.setSourceMetadata(0.0, -5.0);
        REQUIRE(clip.audio().interpretation.totalBeats == 0.0);
        REQUIRE(clip.audio().interpretation.bpm == 0.0);
    }

    SECTION("Sets one field independently of the other") {
        clip.audio().interpretation.bpm = 140.0;  // already set
        clip.setSourceMetadata(4.0, 120.0);
        REQUIRE(clip.audio().interpretation.totalBeats == 4.0);  // was unset, gets populated
        REQUIRE(clip.audio().interpretation.bpm == 140.0);       // was set, not overwritten
    }
}

// ─────────────────────────────────────────────────────────────
// ClipOperations::setAutoTempo — with real detected BPM
// When source interpretation BPM differs from project BPM, it's a real detected
// BPM and should NOT be calibrated. lengthBeats preserves the
// clip's current timeline length (not source beats).
// ─────────────────────────────────────────────────────────────

// Issue #1157: when the file carries source interpretation data,
// setAutoTempo defaults placement length to that beat extent so a freshly-dropped loop
// becomes its natural musical length, not (length × projectBPM / 60).
static constexpr double AMEN_EXPECTED_LENGTH_BEATS = AMEN_SOURCE_BEATS;

TEST_CASE("setAutoTempo - preserves real detected BPM", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();

    SECTION("source interpretation BPM preserved when it differs from project BPM") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.audio().interpretation.bpm == Approx(AMEN_ORIGINAL_BPM));
    }

    SECTION("source interpretation total beats preserved when BPM differs from project BPM") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.audio().interpretation.totalBeats == Approx(AMEN_SOURCE_BEATS));
    }

    SECTION("lengthBeats preserves timeline length in project beats") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.lengthBeats == Approx(AMEN_EXPECTED_LENGTH_BEATS));
    }

    SECTION("lengthBeats == loopLengthBeats at initial setup (no sub-loop)") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        // Slight tolerance because AMEN_DURATION (1.513s) doesn't exactly
        // round-trip through AMEN_ORIGINAL_BPM (158.6) to give 4.0 source
        // beats — within a thousandth.
        REQUIRE(clip.lengthBeats == Approx(clip.loopLengthBeats).margin(0.01));
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
// Source interpretation calibration — only when BPM approximately matches project BPM
// (i.e. interpretation BPM was defaulted from project, not detected)
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - calibrates when source interpretation BPM matches project",
          "[clip][auto-tempo]") {
    SECTION("source interpretation BPM stays at project BPM when they match") {
        auto clip = makeCalibratedClip(120.0);
        ClipOperations::setAutoTempo(clip, true, 120.0);
        REQUIRE(clip.audio().interpretation.bpm == Approx(120.0));
    }

    SECTION("source interpretation BPM equals project BPM / speedRatio when appropriate") {
        auto clip = makeCalibratedClip(120.0);
        clip.speedRatio = 2.0;
        // effectiveBPM = 120/2 = 60, but interpretation BPM = 120, so no calibration
        // Actually this is the "differs" case so calibration is skipped
        ClipOperations::setAutoTempo(clip, true, 120.0);
        REQUIRE(clip.audio().interpretation.bpm == Approx(120.0));  // preserved
    }

    SECTION("Calibration when source interpretation BPM was unknown (zero)") {
        auto clip = makeAmenClip();
        clip.audio().interpretation.bpm = 0.0;
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        // Unknown interpretation BPM stays unknown; the fallback compute path is used.
    }
}

// ─────────────────────────────────────────────────────────────
// getAutoTempoBeatRange — returns stored source beats
// ─────────────────────────────────────────────────────────────

TEST_CASE("getAutoTempoBeatRange - source beat range", "[clip][auto-tempo][te-sync]") {
    SECTION("Returns stored loopLengthBeats when set") {
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);
        REQUIRE(lengthBeats == Approx(clip.loopLengthBeats));
    }

    SECTION("Beat range maps to file's natural beat count") {
        // Issue #1157: in beat mode, beats are beats. The loop range returned
        // here is the source's musical extent for a fresh
        // clip with the whole file as the loop region. Margin allows for
        // AMEN_DURATION/AMEN_ORIGINAL_BPM rounding (~4.0008 vs 4.0).
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        REQUIRE(lengthBeats == Approx(AMEN_SOURCE_BEATS).margin(0.01));
    }

    SECTION("Returns {0,0} when autoTempo is off") {
        auto clip = makeAmenClip();
        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        REQUIRE(startBeats == 0.0);
        REQUIRE(lengthBeats == 0.0);
    }
}

// ─────────────────────────────────────────────────────────────
// getEndBeats — consistent with model state
// ─────────────────────────────────────────────────────────────

TEST_CASE("getEndBeats - consistent in auto-tempo mode", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    clip.startTime = 0.0;
    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    SECTION("getEndBeats matches startBeats + lengthBeats") {
        REQUIRE(clip.getEndBeats(PROJECT_BPM) == Approx(clip.startBeats + clip.lengthBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// setAutoTempo with offset — preserves loop region
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - with offset preserves loop start", "[clip][auto-tempo][offset]") {
    auto clip = makeAmenClip();
    clip.offset = 0.5;

    SECTION("loopStart set to offset when loop was not enabled") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(clip.loopStart == Approx(0.5));
    }

    SECTION("Clamping shifts start when loop exceeds file with offset") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, PROJECT_BPM);

        // Beat range must be non-negative
        REQUIRE(startBeats >= 0.0);
        REQUIRE(lengthBeats > 0.0);
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

    SECTION("loopLengthBeats is in source beats") {
        // Issue #1157: beats are beats. loopLengthBeats describes the loop
        // region's musical extent in beats, regardless of project tempo. For
        // a 0.8-second loop in a file interpreted at 158.6 BPM, that's
        // 0.8 × 158.6 / 60 ≈ 2.115 beats.
        double expectedLoopBeats = 0.8 * AMEN_ORIGINAL_BPM / 60.0;
        REQUIRE(clip.loopLengthBeats == Approx(expectedLoopBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// Round-trip: enable → disable → enable
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - disable preserves source seconds", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    clip.loopEnabled = true;
    clip.loopStart = 0.25;
    clip.loopLength = 0.75;
    clip.offset = 0.5;

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    // Verify beat values were set and are authoritative in auto-tempo mode.
    REQUIRE(clip.lengthBeats > 0.0);
    REQUIRE(clip.loopLengthBeats > 0.0);
    REQUIRE(clip.startBeats >= 0.0);

    const double expectedOffset = clip.getSourceOffset();
    const double expectedLoopStart = clip.getSourceLoopStart();
    const double expectedLoopLength = clip.getSourceLoopLength();
    const double expectedPhase = wrapPhase(expectedOffset - expectedLoopStart, expectedLoopLength);

    ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);

    SECTION("Source seconds are preserved") {
        REQUIRE(clip.offset == Approx(expectedLoopStart + expectedPhase));
        REQUIRE(clip.loopStart == Approx(expectedLoopStart));
        REQUIRE(clip.loopLength == Approx(expectedLoopLength));
    }

    SECTION("Source beat-loop values are cleared") {
        REQUIRE(clip.loopStartBeats == 0.0);
        REQUIRE(clip.loopLengthBeats == 0.0);
    }

    SECTION("autoTempo is false") {
        REQUIRE_FALSE(clip.autoTempo);
    }
}

TEST_CASE("setAutoTempo - re-enable preserves trimmed placement length", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    clip.audio().source.durationSeconds = AMEN_DURATION;
    clip.loopEnabled = true;

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
    REQUIRE(clip.placement.lengthBeats == Approx(AMEN_SOURCE_BEATS));

    constexpr double trimmedLengthBeats = 0.5;
    clip.setPlacementBeats(clip.placement.startBeat, trimmedLengthBeats);
    clip.deriveTimesFromBeats(PROJECT_BPM);

    ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);
    REQUIRE_FALSE(clip.autoTempo);
    REQUIRE(clip.placement.lengthBeats == Approx(trimmedLengthBeats));

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
    REQUIRE(clip.autoTempo);
    REQUIRE(clip.placement.lengthBeats == Approx(trimmedLengthBeats));
    REQUIRE(clip.length == Approx(trimmedLengthBeats * 60.0 / PROJECT_BPM));
}

TEST_CASE("setAutoTempo - no-op when already in target state", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();

    SECTION("Enable when already enabled is no-op") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        double savedLength = clip.length;
        double savedLengthBeats = clip.lengthBeats;
        double savedLoopLengthBeats = clip.loopLengthBeats;

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        REQUIRE(clip.length == Approx(savedLength));
        REQUIRE(clip.lengthBeats == Approx(savedLengthBeats));
        REQUIRE(clip.loopLengthBeats == Approx(savedLoopLengthBeats));
    }

    SECTION("Disable when already disabled is no-op") {
        REQUIRE_FALSE(clip.autoTempo);
        ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);
        REQUIRE_FALSE(clip.autoTempo);
    }
}

// ─────────────────────────────────────────────────────────────
// Calibration at different project BPMs — only applies when
// Source interpretation BPM matches project BPM (defaulted, not detected)
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - calibration with matching source interpretation BPM",
          "[clip][auto-tempo]") {
    SECTION("At 120 BPM, source interpretation BPM preserved when it matches project") {
        auto clip = makeCalibratedClip(120.0);
        ClipOperations::setAutoTempo(clip, true, 120.0);

        REQUIRE(clip.audio().interpretation.bpm == Approx(120.0));
        REQUIRE(clip.length == Approx(2.0));
        REQUIRE(clip.lengthBeats == Approx(4.0));
        REQUIRE(clip.loopLengthBeats == Approx(4.0));
    }

    SECTION("At 60 BPM with matching source interpretation BPM, calibrates to 60") {
        auto clip = makeCalibratedClip(60.0);
        clip.length = 4.0;  // 4 beats at 60 BPM

        ClipOperations::setAutoTempo(clip, true, 60.0);

        REQUIRE(clip.audio().interpretation.bpm == Approx(60.0));
        REQUIRE(60.0 / clip.audio().interpretation.bpm == Approx(1.0));
    }

    SECTION("Real detected BPM (158.6) preserved at any project tempo") {
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, 200.0);

        REQUIRE(clip.audio().interpretation.bpm == Approx(AMEN_ORIGINAL_BPM));
    }
}

// ─────────────────────────────────────────────────────────────
// Regression: loop region wrapping past file end
// ─────────────────────────────────────────────────────────────

TEST_CASE("Regression: loop wrapping past file end", "[clip][auto-tempo][regression]") {
    // 6s file, original BPM 138, project 69
    static constexpr double FILE_DURATION = 6.0;
    static constexpr double FILE_BPM = 138.0;
    static constexpr double FILE_BEATS = FILE_DURATION * FILE_BPM / 60.0;  // 13.8 beats

    ClipInfo clip;
    clip.setAudioContent();
    clip.audio().source.filePath = "long_loop.wav";
    clip.length = FILE_DURATION;
    clip.offset = 5.0;  // near end of file
    clip.speedRatio = 1.0;
    clip.audio().interpretation.bpm = FILE_BPM;
    clip.audio().interpretation.totalBeats = FILE_BEATS;

    ClipOperations::setAutoTempo(clip, true, 69.0);

    auto [startBeats, lengthBeats] = ClipOperations::getAutoTempoBeatRange(clip, 69.0);

    // Beat range must be non-negative
    REQUIRE(startBeats >= 0.0);
    REQUIRE(lengthBeats > 0.0);
}

// ─────────────────────────────────────────────────────────────
// setAutoTempoPlacementLengthBeats — edits timeline placement only
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempoPlacementLengthBeats - extends placement without rewriting source",
          "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    double originalSourceInterpretationBpm = clip.audio().interpretation.bpm;
    double originalSourceBeats = clip.audio().interpretation.totalBeats;
    double originalLoopLengthBeats = clip.loopLengthBeats;

    ClipOperations::setAutoTempoPlacementLengthBeats(clip, originalLoopLengthBeats * 2.0,
                                                     PROJECT_BPM);

    SECTION("source interpretation is unchanged") {
        REQUIRE(clip.audio().interpretation.bpm ==
                Approx(originalSourceInterpretationBpm).margin(0.1));
        REQUIRE(clip.audio().interpretation.totalBeats == Approx(originalSourceBeats).margin(0.01));
    }

    SECTION("loop region stays in source beats") {
        REQUIRE(clip.loopLengthBeats == Approx(originalLoopLengthBeats).margin(0.01));
    }

    SECTION("placement length doubles") {
        REQUIRE(clip.placement.lengthBeats == Approx(originalLoopLengthBeats * 2.0).margin(0.01));
    }
}
