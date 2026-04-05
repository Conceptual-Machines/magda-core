#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/StepClock.hpp"

using StepClock = magda::daw::audio::StepClock;
using Approx = Catch::Approx;

// ============================================================================
// Rate conversion
// ============================================================================

TEST_CASE("StepClock rateToBeats returns correct subdivisions", "[stepclock][rate]") {
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Quarter) == 1.0);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Eighth) == 0.5);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::Sixteenth) == 0.25);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::ThirtySecond) == 0.125);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedQuarter) == 1.5);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedEighth) == 0.75);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::DottedSixteenth) == 0.375);
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletQuarter) == Approx(2.0 / 3.0));
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletEighth) == Approx(1.0 / 3.0));
    REQUIRE(StepClock::rateToBeats(StepClock::Rate::TripletSixteenth) == Approx(0.5 / 3.0));
}

TEST_CASE("StepClock dotted rates are 1.5x their base", "[stepclock][rate]") {
    double quarter = StepClock::rateToBeats(StepClock::Rate::Quarter);
    double dottedQuarter = StepClock::rateToBeats(StepClock::Rate::DottedQuarter);
    REQUIRE(dottedQuarter == Approx(quarter * 1.5));

    double eighth = StepClock::rateToBeats(StepClock::Rate::Eighth);
    double dottedEighth = StepClock::rateToBeats(StepClock::Rate::DottedEighth);
    REQUIRE(dottedEighth == Approx(eighth * 1.5));

    double sixteenth = StepClock::rateToBeats(StepClock::Rate::Sixteenth);
    double dottedSixteenth = StepClock::rateToBeats(StepClock::Rate::DottedSixteenth);
    REQUIRE(dottedSixteenth == Approx(sixteenth * 1.5));
}

TEST_CASE("StepClock triplet rates are 2/3 of their base", "[stepclock][rate]") {
    double quarter = StepClock::rateToBeats(StepClock::Rate::Quarter);
    double tripletQuarter = StepClock::rateToBeats(StepClock::Rate::TripletQuarter);
    REQUIRE(tripletQuarter == Approx(quarter * 2.0 / 3.0));

    double eighth = StepClock::rateToBeats(StepClock::Rate::Eighth);
    double tripletEighth = StepClock::rateToBeats(StepClock::Rate::TripletEighth);
    REQUIRE(tripletEighth == Approx(eighth * 2.0 / 3.0));
}

// ============================================================================
// Ramp curve — bezier mode (smooth)
// ============================================================================

TEST_CASE("StepClock applyRampCurve identity when depth is zero", "[stepclock][ramp]") {
    // With depth=0, curve should be identity (t → t) regardless of skew
    for (float skew : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        for (double t = 0.0; t <= 1.0; t += 0.1) {
            REQUIRE(StepClock::applyRampCurve(t, 0.0f, skew) == Approx(t).margin(0.002));
        }
    }
}

TEST_CASE("StepClock applyRampCurve endpoints are fixed", "[stepclock][ramp]") {
    // Curve always maps 0→0 and 1→1
    for (float depth : {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f}) {
        for (float skew : {-0.8f, 0.0f, 0.8f}) {
            REQUIRE(StepClock::applyRampCurve(0.0, depth, skew) == Approx(0.0).margin(0.001));
            REQUIRE(StepClock::applyRampCurve(1.0, depth, skew) == Approx(1.0).margin(0.001));
        }
    }
}

TEST_CASE("StepClock applyRampCurve is monotonically increasing", "[stepclock][ramp]") {
    for (float depth : {-0.8f, -0.3f, 0.3f, 0.8f}) {
        for (float skew : {-0.5f, 0.0f, 0.5f}) {
            double prev = -1.0;
            for (int i = 0; i <= 100; ++i) {
                double t = static_cast<double>(i) / 100.0;
                double val = StepClock::applyRampCurve(t, depth, skew);
                REQUIRE(val >= prev - 1e-10);
                prev = val;
            }
        }
    }
}

TEST_CASE("StepClock applyRampCurve positive depth bows above diagonal", "[stepclock][ramp]") {
    // At t=0.5, positive depth should produce value > 0.5 (front-loaded)
    double mid = StepClock::applyRampCurve(0.5, 0.5f, 0.0f);
    REQUIRE(mid > 0.5);
}

TEST_CASE("StepClock applyRampCurve negative depth bows below diagonal", "[stepclock][ramp]") {
    // At t=0.5, negative depth should produce value < 0.5 (back-loaded)
    double mid = StepClock::applyRampCurve(0.5, -0.5f, 0.0f);
    REQUIRE(mid < 0.5);
}

TEST_CASE("StepClock applyRampCurve output stays in [0, 1]", "[stepclock][ramp]") {
    for (float depth : {-0.99f, -0.5f, 0.0f, 0.5f, 0.99f}) {
        for (float skew : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            for (int i = 0; i <= 100; ++i) {
                double t = static_cast<double>(i) / 100.0;
                double val = StepClock::applyRampCurve(t, depth, skew);
                REQUIRE(val >= -0.01);
                REQUIRE(val <= 1.01);
            }
        }
    }
}

// ============================================================================
// Ramp curve — hard angle (piecewise linear)
// ============================================================================

TEST_CASE("StepClock hard angle endpoints are fixed", "[stepclock][hardangle]") {
    for (float depth : {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f}) {
        for (float skew : {-0.8f, 0.0f, 0.8f}) {
            REQUIRE(StepClock::applyRampCurve(0.0, depth, skew, true) == Approx(0.0).margin(0.001));
            REQUIRE(StepClock::applyRampCurve(1.0, depth, skew, true) == Approx(1.0).margin(0.001));
        }
    }
}

TEST_CASE("StepClock hard angle identity when depth is zero", "[stepclock][hardangle]") {
    for (float skew : {-1.0f, 0.0f, 1.0f}) {
        for (double t = 0.0; t <= 1.0; t += 0.1) {
            REQUIRE(StepClock::applyRampCurve(t, 0.0f, skew, true) == Approx(t).margin(0.002));
        }
    }
}

TEST_CASE("StepClock hard angle is monotonically increasing", "[stepclock][hardangle]") {
    for (float depth : {-0.8f, -0.3f, 0.3f, 0.8f}) {
        for (float skew : {-0.5f, 0.0f, 0.5f}) {
            double prev = -1.0;
            for (int i = 0; i <= 100; ++i) {
                double t = static_cast<double>(i) / 100.0;
                double val = StepClock::applyRampCurve(t, depth, skew, true);
                REQUIRE(val >= prev - 1e-10);
                prev = val;
            }
        }
    }
}

TEST_CASE("StepClock hard angle passes through control point", "[stepclock][hardangle]") {
    float depth = 0.4f;
    float skew = 0.2f;
    // Control point x = 0.5 + skew * 0.49 = 0.598
    // Control point y = s + depth = 0.598 + 0.4 = 0.998
    double s = 0.5 + static_cast<double>(skew) * 0.49;
    double expectedY = s + static_cast<double>(depth);
    double val = StepClock::applyRampCurve(s, depth, skew, true);
    REQUIRE(val == Approx(expectedY).margin(0.001));
}

TEST_CASE("StepClock hard angle is piecewise linear", "[stepclock][hardangle]") {
    // Two line segments should have constant slope within each segment
    float depth = 0.3f;
    float skew = 0.0f;
    double s = 0.5 + static_cast<double>(skew) * 0.49;

    // Check linearity in first segment (0 to s)
    double y_quarter = StepClock::applyRampCurve(s * 0.25, depth, skew, true);
    double y_half = StepClock::applyRampCurve(s * 0.5, depth, skew, true);
    double y_three_quarter = StepClock::applyRampCurve(s * 0.75, depth, skew, true);
    // Midpoint should be average of quarter and three-quarter
    REQUIRE(y_half == Approx((y_quarter + y_three_quarter) / 2.0).margin(0.001));
}

TEST_CASE("StepClock hard angle vs bezier differ at midpoints", "[stepclock][hardangle]") {
    // Hard angle and smooth bezier should give different results for non-zero depth
    float depth = 0.5f;
    float skew = 0.0f;
    double t = 0.25;
    double bezier = StepClock::applyRampCurve(t, depth, skew, false);
    double linear = StepClock::applyRampCurve(t, depth, skew, true);
    REQUIRE(bezier != Approx(linear).margin(0.01));
}

// ============================================================================
// Ramp curve — symmetry and skew
// ============================================================================

TEST_CASE("StepClock applyRampCurve with zero depth and zero skew is symmetric about midpoint",
          "[stepclock][ramp]") {
    // Symmetry f(0.5-dt) + f(0.5+dt) == 1.0 only holds when depth=0 (identity curve).
    // With depth != 0 the control point moves off the diagonal, breaking symmetry.
    float depth = 0.0f;
    for (double dt = 0.05; dt < 0.5; dt += 0.05) {
        double lo = StepClock::applyRampCurve(0.5 - dt, depth, 0.0f);
        double hi = StepClock::applyRampCurve(0.5 + dt, depth, 0.0f);
        REQUIRE(lo + hi == Approx(1.0).margin(0.01));
    }
}

TEST_CASE("StepClock applyRampCurve skew shifts the inflection point", "[stepclock][ramp]") {
    // Positive skew shifts the control point right → left half is flatter (lower values).
    // At t=0.3, negative skew (control point shifted left) bends the curve up earlier,
    // giving higher values than positive skew.
    float depth = 0.3f;
    double valPosSkew = StepClock::applyRampCurve(0.3, depth, 0.5f);
    double valNegSkew = StepClock::applyRampCurve(0.3, depth, -0.5f);
    REQUIRE(valNegSkew > valPosSkew);
}
