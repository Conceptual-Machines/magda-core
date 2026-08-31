#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/ui/components/pianoroll/VelocityLaneUtils.hpp"

using namespace magda::velocity_lane;

// ============================================================================
// beatToPixel / pixelToBeat
// ============================================================================

TEST_CASE("beatToPixel - basic conversion", "[velocity_lane]") {
    // 50 pixels per beat, 2px left padding, 0 scroll
    REQUIRE(beatToPixel(0.0, 50.0, 2, 0) == 2);
    REQUIRE(beatToPixel(1.0, 50.0, 2, 0) == 52);
    REQUIRE(beatToPixel(2.0, 50.0, 2, 0) == 102);
}

TEST_CASE("beatToPixel - with scroll offset", "[velocity_lane]") {
    // Scrolled 100px to the right
    REQUIRE(beatToPixel(0.0, 50.0, 2, 100) == -98);
    REQUIRE(beatToPixel(2.0, 50.0, 2, 100) == 2);
}

TEST_CASE("pixelToBeat - basic conversion", "[velocity_lane]") {
    REQUIRE(pixelToBeat(2, 50.0, 2, 0) == Catch::Approx(0.0));
    REQUIRE(pixelToBeat(52, 50.0, 2, 0) == Catch::Approx(1.0));
    REQUIRE(pixelToBeat(102, 50.0, 2, 0) == Catch::Approx(2.0));
}

TEST_CASE("beatToPixel and pixelToBeat are inverses", "[velocity_lane]") {
    double ppb = 80.0;
    int padding = 5;
    int scroll = 30;

    for (double beat : {0.0, 1.0, 2.5, 10.0}) {
        int px = beatToPixel(beat, ppb, padding, scroll);
        double roundTrip = pixelToBeat(px, ppb, padding, scroll);
        // Allow rounding error from int truncation
        REQUIRE(roundTrip == Catch::Approx(beat).margin(1.0 / ppb));
    }
}

// ============================================================================
// velocityToY / yToVelocity
// ============================================================================

TEST_CASE("velocityToY - boundary values", "[velocity_lane]") {
    int height = 80;
    int margin = 2;
    int usableHeight = height - (margin * 2);  // 76

    // Velocity 0 should be at bottom (margin + usableHeight)
    REQUIRE(velocityToY(0, height, margin) == margin + usableHeight);

    // Velocity 127 should be at top (margin)
    REQUIRE(velocityToY(127, height, margin) == margin);
}

TEST_CASE("yToVelocity - boundary values", "[velocity_lane]") {
    int height = 80;
    int margin = 2;

    // Top of usable area = velocity 127
    REQUIRE(yToVelocity(margin, height, margin) == 127);

    // Bottom of usable area = velocity 0
    int bottom = height - margin;
    REQUIRE(yToVelocity(bottom, height, margin) == 0);
}

TEST_CASE("yToVelocity - clamps to valid range", "[velocity_lane]") {
    int height = 80;
    REQUIRE(yToVelocity(-100, height) >= 0);
    REQUIRE(yToVelocity(-100, height) <= 127);
    REQUIRE(yToVelocity(1000, height) >= 0);
    REQUIRE(yToVelocity(1000, height) <= 127);
}

TEST_CASE("velocityToY and yToVelocity are approximate inverses", "[velocity_lane]") {
    int height = 100;
    int margin = 2;

    for (int vel : {0, 1, 32, 64, 96, 126, 127}) {
        int y = velocityToY(vel, height, margin);
        int roundTrip = yToVelocity(y, height, margin);
        // Integer division causes up to 1 unit rounding error
        REQUIRE(std::abs(roundTrip - vel) <= 1);
    }
}

// ============================================================================
// interpolateVelocity
// ============================================================================

TEST_CASE("interpolateVelocity - linear ramp", "[velocity_lane]") {
    // Linear (curveAmount = 0): from 0 to 127
    REQUIRE(interpolateVelocity(0.0f, 0, 127) == 0);
    REQUIRE(interpolateVelocity(1.0f, 0, 127) == 127);
    REQUIRE(interpolateVelocity(0.5f, 0, 127) == 63);
}

TEST_CASE("interpolateVelocity - constant ramp", "[velocity_lane]") {
    // Same start and end velocity
    REQUIRE(interpolateVelocity(0.0f, 80, 80) == 80);
    REQUIRE(interpolateVelocity(0.5f, 80, 80) == 80);
    REQUIRE(interpolateVelocity(1.0f, 80, 80) == 80);
}

TEST_CASE("interpolateVelocity - descending ramp", "[velocity_lane]") {
    REQUIRE(interpolateVelocity(0.0f, 127, 0) == 127);
    REQUIRE(interpolateVelocity(1.0f, 127, 0) == 0);
    // Midpoint should be ~63
    REQUIRE(std::abs(interpolateVelocity(0.5f, 127, 0) - 63) <= 1);
}

TEST_CASE("interpolateVelocity - clamps output", "[velocity_lane]") {
    // Even with extreme curve, output stays in 0-127
    REQUIRE(interpolateVelocity(0.5f, 0, 127, 1.0f) >= 0);
    REQUIRE(interpolateVelocity(0.5f, 0, 127, 1.0f) <= 127);
    REQUIRE(interpolateVelocity(0.5f, 0, 127, -1.0f) >= 0);
    REQUIRE(interpolateVelocity(0.5f, 0, 127, -1.0f) <= 127);
}

TEST_CASE("interpolateVelocity - curve bends midpoint", "[velocity_lane]") {
    // Positive curve should push midpoint above the linear midpoint
    int linear = interpolateVelocity(0.5f, 0, 127, 0.0f);
    int curved = interpolateVelocity(0.5f, 0, 127, 0.5f);
    REQUIRE(curved > linear);

    // Negative curve should push midpoint below
    int curvedDown = interpolateVelocity(0.5f, 0, 127, -0.5f);
    REQUIRE(curvedDown < linear);
}

TEST_CASE("interpolateVelocity - endpoints unaffected by curve", "[velocity_lane]") {
    // At t=0 and t=1, bezier always equals the endpoint
    REQUIRE(interpolateVelocity(0.0f, 20, 100, 0.8f) == 20);
    REQUIRE(interpolateVelocity(1.0f, 20, 100, 0.8f) == 100);
    REQUIRE(interpolateVelocity(0.0f, 20, 100, -0.8f) == 20);
    REQUIRE(interpolateVelocity(1.0f, 20, 100, -0.8f) == 100);
}

// ============================================================================
// computeRampVelocities
// ============================================================================

TEST_CASE("computeRampVelocities - fewer than 2 notes returns empty", "[velocity_lane]") {
    REQUIRE(computeRampVelocities({}, 0, 127).empty());
    REQUIRE(computeRampVelocities({1.0}, 0, 127).empty());
}

TEST_CASE("computeRampVelocities - two notes linear", "[velocity_lane]") {
    auto result = computeRampVelocities({0.0, 4.0}, 0, 127);
    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == 0);
    REQUIRE(result[1] == 127);
}

TEST_CASE("computeRampVelocities - evenly spaced notes", "[velocity_lane]") {
    auto result = computeRampVelocities({0.0, 1.0, 2.0, 3.0, 4.0}, 0, 100);
    REQUIRE(result.size() == 5);
    REQUIRE(result[0] == 0);
    REQUIRE(result[4] == 100);
    // Middle note at t=0.5 should be ~50
    REQUIRE(std::abs(result[2] - 50) <= 1);
}

TEST_CASE("computeRampVelocities - notes at same position", "[velocity_lane]") {
    // All notes at same beat — range is 0, all get startVel
    auto result = computeRampVelocities({2.0, 2.0, 2.0}, 60, 120);
    REQUIRE(result.size() == 3);
    for (int v : result) {
        REQUIRE(v == 60);  // t=0 for all
    }
}

TEST_CASE("computeRampVelocities - with curve", "[velocity_lane]") {
    auto linear = computeRampVelocities({0.0, 2.0, 4.0}, 0, 100, 0.0f);
    auto curved = computeRampVelocities({0.0, 2.0, 4.0}, 0, 100, 0.5f);

    REQUIRE(linear.size() == 3);
    REQUIRE(curved.size() == 3);

    // Endpoints same
    REQUIRE(linear[0] == curved[0]);
    REQUIRE(linear[2] == curved[2]);

    // Midpoint different due to curve
    REQUIRE(curved[1] > linear[1]);
}
