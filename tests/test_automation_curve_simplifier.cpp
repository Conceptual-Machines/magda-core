#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "../magda/daw/audio/AutomationCurveSimplifier.hpp"

using namespace magda;
using Point = AutomationCurveSimplifier::Point;

TEST_CASE("AutomationCurveSimplifier - empty and trivial inputs", "[automation][simplify]") {
    SECTION("empty") {
        REQUIRE(AutomationCurveSimplifier::simplify({}, 0.01).empty());
    }
    SECTION("single point") {
        auto keep = AutomationCurveSimplifier::simplify({{0.0, 0.5}}, 0.01);
        REQUIRE(keep == std::vector<size_t>{0});
    }
    SECTION("two points always kept") {
        auto keep = AutomationCurveSimplifier::simplify({{0.0, 0.0}, {1.0, 1.0}}, 0.01);
        REQUIRE(keep == std::vector<size_t>{0, 1});
    }
}

TEST_CASE("AutomationCurveSimplifier - collinear points collapse to endpoints",
          "[automation][simplify]") {
    std::vector<Point> pts = {
        {0.0, 0.0}, {0.25, 0.25}, {0.5, 0.5}, {0.75, 0.75}, {1.0, 1.0},
    };
    auto keep = AutomationCurveSimplifier::simplify(pts, 0.01);
    REQUIRE(keep == std::vector<size_t>{0, 4});
}

TEST_CASE("AutomationCurveSimplifier - noisy line within epsilon collapses",
          "[automation][simplify]") {
    // Straight line with small jitter (all within 0.005 of the line).
    std::vector<Point> pts = {
        {0.0, 0.0},   {0.1, 0.102}, {0.2, 0.198}, {0.3, 0.303},
        {0.4, 0.399}, {0.5, 0.502}, {1.0, 1.0},
    };
    auto keep = AutomationCurveSimplifier::simplify(pts, 0.01);
    REQUIRE(keep.front() == 0);
    REQUIRE(keep.back() == pts.size() - 1);
    REQUIRE(keep.size() == 2);  // All jitter is below epsilon.
}

TEST_CASE("AutomationCurveSimplifier - sharp peak is preserved", "[automation][simplify]") {
    // Triangle wave peak at index 2. The peak deviates massively from the
    // flat endpoint-line, so it must be kept.
    std::vector<Point> pts = {
        {0.0, 0.0}, {0.25, 0.5}, {0.5, 1.0}, {0.75, 0.5}, {1.0, 0.0},
    };
    auto keep = AutomationCurveSimplifier::simplify(pts, 0.01);
    // Must include 0, peak (2), and last (4). Mid-ramps may or may not survive.
    REQUIRE(keep.front() == 0);
    REQUIRE(keep.back() == 4);
    bool hasPeak = false;
    for (auto i : keep)
        if (i == 2)
            hasPeak = true;
    REQUIRE(hasPeak);
}

TEST_CASE("AutomationCurveSimplifier - respects epsilon tolerance", "[automation][simplify]") {
    // A deviation of 0.05 from the line.
    std::vector<Point> pts = {
        {0.0, 0.0},
        {0.5, 0.55},
        {1.0, 1.0},
    };
    SECTION("tight epsilon keeps the middle point") {
        auto keep = AutomationCurveSimplifier::simplify(pts, 0.01);
        REQUIRE(keep.size() == 3);
    }
    SECTION("loose epsilon drops it") {
        auto keep = AutomationCurveSimplifier::simplify(pts, 0.1);
        REQUIRE(keep == std::vector<size_t>{0, 2});
    }
}

TEST_CASE("AutomationCurveSimplifier - endpoints always retained even on flat curve",
          "[automation][simplify]") {
    std::vector<Point> pts = {
        {0.0, 0.3},
        {0.5, 0.3},
        {1.0, 0.3},
    };
    auto keep = AutomationCurveSimplifier::simplify(pts, 0.01);
    REQUIRE(keep.front() == 0);
    REQUIRE(keep.back() == 2);
}

TEST_CASE("AutomationCurveSimplifier - dense sine-like curve reduces substantially",
          "[automation][simplify]") {
    // Half sine from 0 to 1 with 41 samples. Simplification should keep
    // enough points to follow the curvature but drop many redundant ones.
    std::vector<Point> pts;
    pts.reserve(41);
    for (int i = 0; i <= 40; ++i) {
        double t = i / 40.0;
        double v = 0.5 - (0.5 * std::cos(t * 3.14159265358979));
        pts.push_back({t, v});
    }
    auto keep = AutomationCurveSimplifier::simplify(pts, 0.01);
    REQUIRE(keep.front() == 0);
    REQUIRE(keep.back() == 40);
    // Should significantly reduce (well under half the original).
    REQUIRE(keep.size() < 20);
    REQUIRE(keep.size() >= 3);
}
