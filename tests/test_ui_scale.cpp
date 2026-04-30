#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/UIScale.hpp"

using Catch::Approx;
using magda::kUIScaleSteps;
using magda::stepUIScale;

// ---------------------------------------------------------------------------
// Exact step values — stepping moves to the adjacent table entry.
// ---------------------------------------------------------------------------

TEST_CASE("stepUIScale - up from exact 1.0 lands on 1.25", "[ui_scale]") {
    REQUIRE(stepUIScale(1.0, +1) == Approx(1.25));
}

TEST_CASE("stepUIScale - up from exact 1.5 lands on 1.75", "[ui_scale]") {
    REQUIRE(stepUIScale(1.5, +1) == Approx(1.75));
}

TEST_CASE("stepUIScale - down from exact 1.5 lands on 1.25", "[ui_scale]") {
    REQUIRE(stepUIScale(1.5, -1) == Approx(1.25));
}

TEST_CASE("stepUIScale - down from exact 2.0 lands on 1.75", "[ui_scale]") {
    REQUIRE(stepUIScale(2.0, -1) == Approx(1.75));
}

// ---------------------------------------------------------------------------
// Between-step values snap to the nearest, then move from there.
// ---------------------------------------------------------------------------

TEST_CASE("stepUIScale - up from 1.3 (nearest 1.25) lands on 1.5", "[ui_scale]") {
    REQUIRE(stepUIScale(1.3, +1) == Approx(1.5));
}

TEST_CASE("stepUIScale - up from 1.4 (nearest 1.5) lands on 1.75", "[ui_scale]") {
    REQUIRE(stepUIScale(1.4, +1) == Approx(1.75));
}

TEST_CASE("stepUIScale - down from 1.4 (nearest 1.5) lands on 1.25", "[ui_scale]") {
    REQUIRE(stepUIScale(1.4, -1) == Approx(1.25));
}

// ---------------------------------------------------------------------------
// Boundary clamping — stepping past the ends of the table stays put.
// ---------------------------------------------------------------------------

TEST_CASE("stepUIScale - down from 1.0 clamps to 1.0", "[ui_scale]") {
    REQUIRE(stepUIScale(1.0, -1) == Approx(1.0));
}

TEST_CASE("stepUIScale - up from 2.0 clamps to 2.0", "[ui_scale]") {
    REQUIRE(stepUIScale(2.0, +1) == Approx(2.0));
}

// ---------------------------------------------------------------------------
// Out-of-range current values — snap to nearest in-range step, then move.
// ---------------------------------------------------------------------------

TEST_CASE("stepUIScale - up from 0.5 (below range) snaps to 1.0 then to 1.25", "[ui_scale]") {
    REQUIRE(stepUIScale(0.5, +1) == Approx(1.25));
}

TEST_CASE("stepUIScale - down from 0.5 (below range) snaps to 1.0 and clamps", "[ui_scale]") {
    REQUIRE(stepUIScale(0.5, -1) == Approx(1.0));
}

TEST_CASE("stepUIScale - down from 3.0 (above range) snaps to 2.0 then to 1.75", "[ui_scale]") {
    REQUIRE(stepUIScale(3.0, -1) == Approx(1.75));
}

TEST_CASE("stepUIScale - up from 3.0 (above range) snaps to 2.0 and clamps", "[ui_scale]") {
    REQUIRE(stepUIScale(3.0, +1) == Approx(2.0));
}

// ---------------------------------------------------------------------------
// Sanity: the step table itself.
// ---------------------------------------------------------------------------

TEST_CASE("kUIScaleSteps is monotonically increasing and non-empty", "[ui_scale]") {
    REQUIRE_FALSE(kUIScaleSteps.empty());
    for (size_t i = 1; i < kUIScaleSteps.size(); ++i) {
        REQUIRE(kUIScaleSteps[i] > kUIScaleSteps[i - 1]);
    }
}
