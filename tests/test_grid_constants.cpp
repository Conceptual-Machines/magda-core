#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/ui/state/TimelineState.hpp"

using magda::GridConstants;
using magda::GridQuantize;

// ============================================================================
// gridAlignsWithBars
// ============================================================================

TEST_CASE("gridAlignsWithBars - 1/4 note (1.0 beat) in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(1.0, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/8 note (0.5 beat) in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.5, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/16 note (0.25 beat) in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.25, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 3/16 note (0.75 beat) does NOT align in 4/4", "[grid_constants]") {
    // fmod(4.0, 0.75) ≈ 0.25, which is not near 0 or 0.75
    REQUIRE(GridConstants::gridAlignsWithBars(0.75, 4.0) == false);
}

TEST_CASE("gridAlignsWithBars - 1/6 note (0.667 beat) aligns with bars (6 fit in 4 beats)",
          "[grid_constants]") {
    double interval = 4.0 / 6.0;  // ~0.6667, divides 4.0 evenly
    REQUIRE(GridConstants::gridAlignsWithBars(interval, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 0.3 beats does NOT align in 4/4", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.3, 4.0) == false);
}

TEST_CASE("gridAlignsWithBars - 2 bars (8.0 beats) aligns in 4/4", "[grid_constants]") {
    // intervalBeats >= barLengthBeats → true
    REQUIRE(GridConstants::gridAlignsWithBars(8.0, 4.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/4 note in 3/4 time", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(1.0, 3.0) == true);
}

TEST_CASE("gridAlignsWithBars - 1/8 note in 3/4 time", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBars(0.5, 3.0) == true);
}

// ============================================================================
// gridAlignsWithBeats
// ============================================================================

TEST_CASE("gridAlignsWithBeats - 1/4 note (1.0 beat)", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(1.0) == true);
}

TEST_CASE("gridAlignsWithBeats - 1/8 note (0.5 beat)", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.5) == true);
}

TEST_CASE("gridAlignsWithBeats - 1/16 note (0.25 beat)", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.25) == true);
}

TEST_CASE("gridAlignsWithBeats - 3/16 note (0.75 beat) does NOT align", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.75) == false);
}

TEST_CASE("gridAlignsWithBeats - 1/6 note (0.667 beat) does NOT align with beats",
          "[grid_constants]") {
    double interval = 4.0 / 6.0;  // ~0.6667, does NOT divide 1.0 evenly
    REQUIRE(GridConstants::gridAlignsWithBeats(interval) == false);
}

TEST_CASE("gridAlignsWithBeats - 0.3 beats does NOT align", "[grid_constants]") {
    REQUIRE(GridConstants::gridAlignsWithBeats(0.3) == false);
}

TEST_CASE("gridAlignsWithBeats - 2 beats (1/2 note)", "[grid_constants]") {
    // intervalBeats >= 1.0 → true
    REQUIRE(GridConstants::gridAlignsWithBeats(2.0) == true);
}

// ============================================================================
// classifyBeatPosition
// ============================================================================

TEST_CASE("classifyBeatPosition - beat 0.0 is bar and beat start in 4/4", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(0.0, 4.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 1.0 is beat but not bar in 4/4", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(1.0, 4.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 4.0 is bar start in 4/4", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(4.0, 4.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 0.5 is subdivision only", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(0.5, 4.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == false);
}

TEST_CASE("classifyBeatPosition - beat 3.9999 is bar start (two-sided tolerance)",
          "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(3.9999, 4.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 0.9999 is beat start (two-sided tolerance)",
          "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(0.9999, 4.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 3.0 in 3/4 is bar start", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(3.0, 3.0);
    REQUIRE(c.isBar == true);
    REQUIRE(c.isBeat == true);
}

TEST_CASE("classifyBeatPosition - beat 2.0 in 3/4 is beat, not bar", "[grid_constants]") {
    auto c = GridConstants::classifyBeatPosition(2.0, 3.0);
    REQUIRE(c.isBar == false);
    REQUIRE(c.isBeat == true);
}

// ============================================================================
// computeGridInterval
// ============================================================================

TEST_CASE("computeGridInterval - manual mode 1/8 returns 0.5", "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = false;
    gq.numerator = 1;
    gq.denominator = 8;
    REQUIRE(GridConstants::computeGridInterval(gq, 100.0, 4, 10) == Catch::Approx(0.5));
}

TEST_CASE("computeGridInterval - manual mode 3/16 returns 0.75", "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = false;
    gq.numerator = 3;
    gq.denominator = 16;
    REQUIRE(GridConstants::computeGridInterval(gq, 100.0, 4, 10) == Catch::Approx(0.75));
}

TEST_CASE("computeGridInterval - auto mode high zoom returns beat subdivision",
          "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = true;
    // zoom=100 ppb, minPixels=10 → 0.125 beats * 100 = 12.5px ≥ 10 → should pick a small
    // subdivision
    double interval = GridConstants::computeGridInterval(gq, 100.0, 4, 10);
    REQUIRE(interval > 0.0);
    REQUIRE(interval <= 1.0);  // Should be a beat subdivision, not bar multiple
}

TEST_CASE("computeGridInterval - auto mode low zoom falls to bar multiples", "[grid_constants]") {
    GridQuantize gq;
    gq.autoGrid = true;
    // zoom=1 ppb, minPixels=10 → even 2 beats * 1 = 2px < 10, so must go to bar multiples
    double interval = GridConstants::computeGridInterval(gq, 1.0, 4, 10);
    // Should be a bar multiple: timeSigNumerator * mult
    REQUIRE(interval >= 4.0);  // At least 1 bar in 4/4
}
