#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "AudioClipTestHelpers.hpp"
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
    magda::test::giveAudioEvent(clip, "amen_break.wav");
    clip.startTime = startTime;
    clip.length = AMEN_DURATION;  // original duration before stretching
    magda::test::audioEvent(clip).setAnchorSeconds(0.0);
    magda::test::audioEvent(clip).speedRatio = 1.0;
    magda::test::audioEvent(clip).interpBpm = AMEN_ORIGINAL_BPM;
    magda::test::audioEvent(clip).interpTotalBeats = AMEN_SOURCE_BEATS;
    return clip;
}

// Helper: make a clip where source interpretation BPM matches project BPM
static ClipInfo makeCalibratedClip(double projectBPM = 120.0) {
    ClipInfo clip;
    clip.setAudioContent();
    magda::test::giveAudioEvent(clip, "sample.wav");
    clip.startTime = 0.0;
    clip.length = 2.0;
    magda::test::audioEvent(clip).setAnchorSeconds(0.0);
    magda::test::audioEvent(clip).speedRatio = 1.0;
    magda::test::audioEvent(clip).interpBpm = projectBPM;  // matches project → calibration applies
    magda::test::audioEvent(clip).interpTotalBeats = 4.0;
    return clip;
}

// ─────────────────────────────────────────────────────────────
// AudioEvent::seedInterpretation
// ─────────────────────────────────────────────────────────────

TEST_CASE("AudioEvent::seedInterpretation - populates unset fields",
          "[clip][auto-tempo][metadata]") {
    ClipInfo clip;
    magda::test::giveAudioEvent(clip, "seed.wav");

    SECTION("Sets both fields when unset") {
        magda::test::audioEvent(clip).seedInterpretation(4.0, 120.0);
        REQUIRE(magda::test::audioEvent(clip).interpTotalBeats == 4.0);
        REQUIRE(magda::test::audioEvent(clip).interpBpm == 120.0);
    }

    SECTION("Does not overwrite existing values") {
        magda::test::audioEvent(clip).interpTotalBeats = 8.0;
        magda::test::audioEvent(clip).interpBpm = 140.0;
        magda::test::audioEvent(clip).seedInterpretation(4.0, 120.0);
        REQUIRE(magda::test::audioEvent(clip).interpTotalBeats == 8.0);
        REQUIRE(magda::test::audioEvent(clip).interpBpm == 140.0);
    }

    SECTION("Ignores zero/negative input") {
        magda::test::audioEvent(clip).seedInterpretation(0.0, -5.0);
        REQUIRE(magda::test::audioEvent(clip).interpTotalBeats == 0.0);
        REQUIRE(magda::test::audioEvent(clip).interpBpm == 0.0);
    }

    SECTION("Sets one field independently of the other") {
        magda::test::audioEvent(clip).interpBpm = 140.0;  // already set
        magda::test::audioEvent(clip).seedInterpretation(4.0, 120.0);
        REQUIRE(magda::test::audioEvent(clip).interpTotalBeats ==
                4.0);                                               // was unset, gets populated
        REQUIRE(magda::test::audioEvent(clip).interpBpm == 140.0);  // was set, not overwritten
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
        REQUIRE(magda::test::audioEvent(clip).interpBpm == Approx(AMEN_ORIGINAL_BPM));
    }

    SECTION("source interpretation total beats preserved when BPM differs from project BPM") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(magda::test::audioEvent(clip).interpTotalBeats == Approx(AMEN_SOURCE_BEATS));
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
        REQUIRE(clip.lengthBeats ==
                Approx(magda::test::audioEvent(clip).loopLengthBeats()).margin(0.01));
    }

    SECTION("startBeats is in project beats") {
        clip.startTime = 3.478;  // exactly 4 beats at 69 BPM
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        double expectedStartBeats = (3.478 * PROJECT_BPM) / 60.0;
        REQUIRE(clip.startBeats == Approx(expectedStartBeats));
    }

    SECTION("speedRatio forced to 1.0") {
        magda::test::audioEvent(clip).speedRatio = 2.0;
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(magda::test::audioEvent(clip).speedRatio == 1.0);
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
        REQUIRE(magda::test::audioEvent(clip).interpBpm == Approx(120.0));
    }

    SECTION("source interpretation BPM equals project BPM / speedRatio when appropriate") {
        auto clip = makeCalibratedClip(120.0);
        magda::test::audioEvent(clip).speedRatio = 2.0;
        // effectiveBPM = 120/2 = 60, but interpretation BPM = 120, so no calibration
        // Actually this is the "differs" case so calibration is skipped
        ClipOperations::setAutoTempo(clip, true, 120.0);
        REQUIRE(magda::test::audioEvent(clip).interpBpm == Approx(120.0));  // preserved
    }

    SECTION("Calibration when source interpretation BPM was unknown (zero)") {
        auto clip = makeAmenClip();
        magda::test::audioEvent(clip).interpBpm = 0.0;
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

        auto [startBeats, lengthBeats] =
            ClipOperations::getAutoTempoBeatRange(magda::test::audioEvent(clip));
        REQUIRE(lengthBeats == Approx(magda::test::audioEvent(clip).loopLengthBeats()));
    }

    SECTION("Beat range maps to file's natural beat count") {
        // Issue #1157: in beat mode, beats are beats. The loop range returned
        // here is the source's musical extent for a fresh
        // clip with the whole file as the loop region. Margin allows for
        // AMEN_DURATION/AMEN_ORIGINAL_BPM rounding (~4.0008 vs 4.0).
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] =
            ClipOperations::getAutoTempoBeatRange(magda::test::audioEvent(clip));

        REQUIRE(lengthBeats == Approx(AMEN_SOURCE_BEATS).margin(0.01));
    }

    SECTION("Returns {0,0} when autoTempo is off") {
        auto clip = makeAmenClip();
        auto [startBeats, lengthBeats] =
            ClipOperations::getAutoTempoBeatRange(magda::test::audioEvent(clip));

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
    magda::test::audioEvent(clip).setAnchorSeconds(0.5);

    SECTION("loopStart set to offset when loop was not enabled") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() == Approx(0.5));
    }

    SECTION("Clamping shifts start when loop exceeds file with offset") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        auto [startBeats, lengthBeats] =
            ClipOperations::getAutoTempoBeatRange(magda::test::audioEvent(clip));

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
    magda::test::audioEvent(clip).setLoopStartSeconds(0.3);
    magda::test::audioEvent(clip).setLoopLengthSeconds(0.8);

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    SECTION("Does not overwrite existing loopStart/loopLength") {
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() == Approx(0.3));
        REQUIRE(magda::test::audioEvent(clip).loopLengthSeconds() == Approx(0.8));
    }

    SECTION("loopLengthBeats is in source beats") {
        // Issue #1157: beats are beats. loopLengthBeats describes the loop
        // region's musical extent in beats, regardless of project tempo. For
        // a 0.8-second loop in a file interpreted at 158.6 BPM, that's
        // 0.8 × 158.6 / 60 ≈ 2.115 beats.
        double expectedLoopBeats = 0.8 * AMEN_ORIGINAL_BPM / 60.0;
        REQUIRE(magda::test::audioEvent(clip).loopLengthBeats() == Approx(expectedLoopBeats));
    }
}

// ─────────────────────────────────────────────────────────────
// Round-trip: enable → disable → enable
// ─────────────────────────────────────────────────────────────

TEST_CASE("setAutoTempo - disable preserves source seconds", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    clip.loopEnabled = true;
    magda::test::audioEvent(clip).setLoopStartSeconds(0.25);
    magda::test::audioEvent(clip).setLoopLengthSeconds(0.75);
    magda::test::audioEvent(clip).setAnchorSeconds(0.5);

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

    // Verify beat values were set and are authoritative in auto-tempo mode.
    REQUIRE(clip.lengthBeats > 0.0);
    REQUIRE(magda::test::audioEvent(clip).loopLengthBeats() > 0.0);
    REQUIRE(clip.startBeats >= 0.0);

    const double expectedOffset = magda::test::audioEvent(clip).anchorSeconds();
    const double expectedLoopStart = magda::test::audioEvent(clip).loopStartSeconds();
    const double expectedLoopLength = magda::test::audioEvent(clip).loopLengthSeconds();
    const double expectedPhase = wrapPhase(expectedOffset - expectedLoopStart, expectedLoopLength);

    ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);

    SECTION("Source seconds are preserved") {
        REQUIRE(magda::test::audioEvent(clip).anchorSeconds() ==
                Approx(expectedLoopStart + expectedPhase));
        REQUIRE(magda::test::audioEvent(clip).loopStartSeconds() == Approx(expectedLoopStart));
        REQUIRE(magda::test::audioEvent(clip).loopLengthSeconds() == Approx(expectedLoopLength));
    }

    SECTION("The source region's beat view follows the preserved seconds") {
        // Beats are a view on the source region, which the section above
        // asserts is preserved, so leaving beat mode cannot zero them. v1
        // cleared separate beat fields here purely because it kept two
        // representations of the same region.
        REQUIRE(magda::test::audioEvent(clip).loopStartBeats() ==
                Approx(expectedLoopStart * magda::test::audioEvent(clip).interpBpm / 60.0));
        REQUIRE(magda::test::audioEvent(clip).loopLengthBeats() ==
                Approx(expectedLoopLength * magda::test::audioEvent(clip).interpBpm / 60.0));
    }

    SECTION("autoTempo is false") {
        REQUIRE_FALSE(magda::test::audioEvent(clip).autoTempo);
    }
}

TEST_CASE("setAutoTempo - re-enable preserves trimmed placement length", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();
    magda::test::setSourceDuration(clip, AMEN_DURATION);
    clip.loopEnabled = true;

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
    REQUIRE(clip.placement.lengthBeats == Approx(AMEN_SOURCE_BEATS));

    constexpr double trimmedLengthBeats = 0.5;
    clip.setPlacementBeats(clip.placement.startBeat, trimmedLengthBeats);
    clip.deriveTimesFromBeats(PROJECT_BPM);

    ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);
    REQUIRE_FALSE(magda::test::audioEvent(clip).autoTempo);
    REQUIRE(clip.placement.lengthBeats == Approx(trimmedLengthBeats));

    ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
    REQUIRE(magda::test::audioEvent(clip).autoTempo);
    REQUIRE(clip.placement.lengthBeats == Approx(trimmedLengthBeats));
    REQUIRE(clip.length == Approx(trimmedLengthBeats * 60.0 / PROJECT_BPM));
}

TEST_CASE("setAutoTempo - no-op when already in target state", "[clip][auto-tempo]") {
    auto clip = makeAmenClip();

    SECTION("Enable when already enabled is no-op") {
        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);
        double savedLength = clip.length;
        double savedLengthBeats = clip.lengthBeats;
        double savedLoopLengthBeats = magda::test::audioEvent(clip).loopLengthBeats();

        ClipOperations::setAutoTempo(clip, true, PROJECT_BPM);

        REQUIRE(clip.length == Approx(savedLength));
        REQUIRE(clip.lengthBeats == Approx(savedLengthBeats));
        REQUIRE(magda::test::audioEvent(clip).loopLengthBeats() == Approx(savedLoopLengthBeats));
    }

    SECTION("Disable when already disabled is no-op") {
        REQUIRE_FALSE(magda::test::audioEvent(clip).autoTempo);
        ClipOperations::setAutoTempo(clip, false, PROJECT_BPM);
        REQUIRE_FALSE(magda::test::audioEvent(clip).autoTempo);
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

        REQUIRE(magda::test::audioEvent(clip).interpBpm == Approx(120.0));
        REQUIRE(clip.length == Approx(2.0));
        REQUIRE(clip.lengthBeats == Approx(4.0));
        REQUIRE(magda::test::audioEvent(clip).loopLengthBeats() == Approx(4.0));
    }

    SECTION("At 60 BPM with matching source interpretation BPM, calibrates to 60") {
        auto clip = makeCalibratedClip(60.0);
        clip.length = 4.0;  // 4 beats at 60 BPM

        ClipOperations::setAutoTempo(clip, true, 60.0);

        REQUIRE(magda::test::audioEvent(clip).interpBpm == Approx(60.0));
        REQUIRE(60.0 / magda::test::audioEvent(clip).interpBpm == Approx(1.0));
    }

    SECTION("Real detected BPM (158.6) preserved at any project tempo") {
        auto clip = makeAmenClip();
        ClipOperations::setAutoTempo(clip, true, 200.0);

        REQUIRE(magda::test::audioEvent(clip).interpBpm == Approx(AMEN_ORIGINAL_BPM));
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
    magda::test::giveAudioEvent(clip, "long_loop.wav");
    clip.length = FILE_DURATION;
    magda::test::audioEvent(clip).setAnchorSeconds(5.0);  // near end of file
    magda::test::audioEvent(clip).speedRatio = 1.0;
    magda::test::audioEvent(clip).interpBpm = FILE_BPM;
    magda::test::audioEvent(clip).interpTotalBeats = FILE_BEATS;

    ClipOperations::setAutoTempo(clip, true, 69.0);

    auto [startBeats, lengthBeats] =
        ClipOperations::getAutoTempoBeatRange(magda::test::audioEvent(clip));

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

    double originalSourceInterpretationBpm = magda::test::audioEvent(clip).interpBpm;
    double originalSourceBeats = magda::test::audioEvent(clip).interpTotalBeats;
    double originalLoopLengthBeats = magda::test::audioEvent(clip).loopLengthBeats();

    ClipOperations::setAutoTempoPlacementLengthBeats(clip, originalLoopLengthBeats * 2.0,
                                                     PROJECT_BPM);

    SECTION("source interpretation is unchanged") {
        REQUIRE(magda::test::audioEvent(clip).interpBpm ==
                Approx(originalSourceInterpretationBpm).margin(0.1));
        REQUIRE(magda::test::audioEvent(clip).interpTotalBeats ==
                Approx(originalSourceBeats).margin(0.01));
    }

    SECTION("loop region stays in source beats") {
        REQUIRE(magda::test::audioEvent(clip).loopLengthBeats() ==
                Approx(originalLoopLengthBeats).margin(0.01));
    }

    SECTION("placement length doubles") {
        REQUIRE(clip.placement.lengthBeats == Approx(originalLoopLengthBeats * 2.0).margin(0.01));
    }
}
