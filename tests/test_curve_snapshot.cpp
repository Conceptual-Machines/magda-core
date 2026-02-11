#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/CurveSnapshot.hpp"

using namespace magda;
using Catch::Approx;

// ============================================================================
// CurveSnapshot::evaluatePreset
// ============================================================================

TEST_CASE("CurveSnapshot::evaluatePreset - boundary values", "[curve][preset]") {
    SECTION("Triangle") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Triangle, 0.0f) == Approx(0.0f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Triangle, 0.25f) == Approx(0.5f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Triangle, 0.5f) == Approx(1.0f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Triangle, 0.75f) == Approx(0.5f));
    }

    SECTION("Sine") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Sine, 0.0f) == Approx(0.5f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Sine, 0.25f) == Approx(1.0f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Sine, 0.5f) ==
                Approx(0.5f).margin(0.001f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Sine, 0.75f) ==
                Approx(0.0f).margin(0.001f));
    }

    SECTION("RampUp") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::RampUp, 0.0f) == Approx(0.0f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::RampUp, 0.5f) == Approx(0.5f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::RampUp, 1.0f) == Approx(1.0f));
    }

    SECTION("RampDown") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::RampDown, 0.0f) == Approx(1.0f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::RampDown, 0.5f) == Approx(0.5f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::RampDown, 1.0f) == Approx(0.0f));
    }

    SECTION("SCurve") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::SCurve, 0.0f) == Approx(0.0f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::SCurve, 0.5f) == Approx(0.5f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::SCurve, 1.0f) == Approx(1.0f));
    }

    SECTION("Exponential starts at 0 and ends at 1") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Exponential, 0.0f) ==
                Approx(0.0f).margin(0.01f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Exponential, 1.0f) ==
                Approx(1.0f).margin(0.01f));
    }

    SECTION("Logarithmic starts at 0 and ends at 1") {
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Logarithmic, 0.0f) ==
                Approx(0.0f).margin(0.01f));
        REQUIRE(CurveSnapshot::evaluatePreset(CurvePreset::Logarithmic, 1.0f) ==
                Approx(1.0f).margin(0.01f));
    }
}

TEST_CASE("CurveSnapshot::evaluatePreset - monotonicity", "[curve][preset]") {
    SECTION("RampUp is monotonically increasing") {
        float prev = -1.0f;
        for (int i = 0; i <= 100; ++i) {
            float phase = static_cast<float>(i) / 100.0f;
            float val = CurveSnapshot::evaluatePreset(CurvePreset::RampUp, phase);
            REQUIRE(val >= prev);
            prev = val;
        }
    }

    SECTION("RampDown is monotonically decreasing") {
        float prev = 2.0f;
        for (int i = 0; i <= 100; ++i) {
            float phase = static_cast<float>(i) / 100.0f;
            float val = CurveSnapshot::evaluatePreset(CurvePreset::RampDown, phase);
            REQUIRE(val <= prev);
            prev = val;
        }
    }

    SECTION("Exponential is monotonically increasing") {
        float prev = -1.0f;
        for (int i = 0; i <= 100; ++i) {
            float phase = static_cast<float>(i) / 100.0f;
            float val = CurveSnapshot::evaluatePreset(CurvePreset::Exponential, phase);
            REQUIRE(val >= prev);
            prev = val;
        }
    }

    SECTION("SCurve is monotonically increasing") {
        float prev = -1.0f;
        for (int i = 0; i <= 100; ++i) {
            float phase = static_cast<float>(i) / 100.0f;
            float val = CurveSnapshot::evaluatePreset(CurvePreset::SCurve, phase);
            REQUIRE(val >= prev);
            prev = val;
        }
    }
}

TEST_CASE("CurveSnapshot::evaluatePreset - output range [0, 1]", "[curve][preset]") {
    auto presets = {CurvePreset::Triangle,   CurvePreset::Sine,   CurvePreset::RampUp,
                    CurvePreset::RampDown,   CurvePreset::SCurve, CurvePreset::Exponential,
                    CurvePreset::Logarithmic};

    for (auto preset : presets) {
        for (int i = 0; i <= 1000; ++i) {
            float phase = static_cast<float>(i) / 1000.0f;
            float val = CurveSnapshot::evaluatePreset(preset, phase);
            REQUIRE(val >= -0.001f);
            REQUIRE(val <= 1.001f);
        }
    }
}

// ============================================================================
// CurveSnapshot::evaluate with custom points
// ============================================================================

TEST_CASE("CurveSnapshot::evaluate - no points falls back to preset", "[curve][evaluate]") {
    CurveSnapshot snap;
    snap.count = 0;
    snap.preset = CurvePreset::RampUp;

    REQUIRE(snap.evaluate(0.0f) == Approx(0.0f));
    REQUIRE(snap.evaluate(0.5f) == Approx(0.5f));
    REQUIRE(snap.evaluate(1.0f) == Approx(1.0f));
}

TEST_CASE("CurveSnapshot::evaluate - single point returns constant", "[curve][evaluate]") {
    CurveSnapshot snap;
    snap.count = 1;
    snap.points[0] = {0.0f, 0.75f, 0.0f};

    REQUIRE(snap.evaluate(0.0f) == Approx(0.75f));
    REQUIRE(snap.evaluate(0.5f) == Approx(0.75f));
    REQUIRE(snap.evaluate(0.99f) == Approx(0.75f));
}

TEST_CASE("CurveSnapshot::evaluate - two-point linear interpolation", "[curve][evaluate]") {
    CurveSnapshot snap;
    snap.count = 2;
    snap.points[0] = {0.0f, 0.0f, 0.0f};  // phase=0, value=0, no tension
    snap.points[1] = {0.5f, 1.0f, 0.0f};  // phase=0.5, value=1

    SECTION("Between first two points") {
        REQUIRE(snap.evaluate(0.0f) == Approx(0.0f));
        REQUIRE(snap.evaluate(0.25f) == Approx(0.5f));
        REQUIRE(snap.evaluate(0.5f) == Approx(1.0f));
    }

    SECTION("Wrap-around segment (0.5 -> 0.0 via 1.0)") {
        // Between point[1] at phase=0.5 and point[0] at phase=0.0 (wrapping)
        REQUIRE(snap.evaluate(0.75f) == Approx(0.5f));
    }
}

TEST_CASE("CurveSnapshot::evaluate - three-point curve", "[curve][evaluate]") {
    CurveSnapshot snap;
    snap.count = 3;
    snap.points[0] = {0.0f, 0.0f, 0.0f};
    snap.points[1] = {0.5f, 1.0f, 0.0f};
    snap.points[2] = {1.0f, 0.0f, 0.0f};

    // Should go: 0->1 in first half, 1->0 in second half (triangle-like)
    // But last segment wraps from point[2] to point[0], both at y=0
    REQUIRE(snap.evaluate(0.0f) == Approx(0.0f));
    REQUIRE(snap.evaluate(0.25f) == Approx(0.5f));
    REQUIRE(snap.evaluate(0.5f) == Approx(1.0f));
    REQUIRE(snap.evaluate(0.75f) == Approx(0.5f));
}

TEST_CASE("CurveSnapshot::evaluate - tension curves interpolation", "[curve][evaluate]") {
    CurveSnapshot snap;
    snap.count = 2;
    snap.points[0] = {0.0f, 0.0f, 0.0f};
    snap.points[1] = {1.0f, 1.0f, 0.0f};

    SECTION("Zero tension is linear") {
        snap.points[0].tension = 0.0f;
        REQUIRE(snap.evaluate(0.5f) == Approx(0.5f));
    }

    SECTION("Positive tension curves below linear") {
        snap.points[0].tension = 2.0f;
        float val = snap.evaluate(0.5f);
        REQUIRE(val < 0.5f);  // Power curve bends down
        REQUIRE(val > 0.0f);
    }

    SECTION("Negative tension curves above linear") {
        snap.points[0].tension = -2.0f;
        float val = snap.evaluate(0.5f);
        REQUIRE(val > 0.5f);  // Inverse power curve bends up
        REQUIRE(val < 1.0f);
    }
}

// ============================================================================
// CurveSnapshotHolder - double buffered update
// ============================================================================

TEST_CASE("CurveSnapshotHolder - update from ModInfo", "[curve][holder]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    mod.curvePreset = CurvePreset::Sine;
    mod.curvePoints.push_back({0.0f, 0.0f, 0.0f});
    mod.curvePoints.push_back({0.5f, 1.0f, 0.5f});
    mod.curvePoints.push_back({1.0f, 0.0f, 0.0f});

    holder.update(mod);

    const CurveSnapshot* snap = holder.active.load();
    REQUIRE(snap->count == 3);
    REQUIRE(snap->preset == CurvePreset::Sine);
    REQUIRE(snap->hasCustomPoints);
    REQUIRE(snap->points[0].phase == Approx(0.0f));
    REQUIRE(snap->points[1].phase == Approx(0.5f));
    REQUIRE(snap->points[1].value == Approx(1.0f));
    REQUIRE(snap->points[1].tension == Approx(0.5f));
}

TEST_CASE("CurveSnapshotHolder - double buffer swaps", "[curve][holder]") {
    CurveSnapshotHolder holder;

    const CurveSnapshot* first = holder.active.load();

    ModInfo mod;
    mod.curvePoints.push_back({0.0f, 0.5f, 0.0f});
    holder.update(mod);

    const CurveSnapshot* second = holder.active.load();
    REQUIRE(second != first);  // Should have swapped to the other buffer

    holder.update(mod);
    const CurveSnapshot* third = holder.active.load();
    REQUIRE(third == first);  // Should swap back to the original buffer
}

TEST_CASE("CurveSnapshotHolder - evaluateCallback", "[curve][holder]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    mod.curvePreset = CurvePreset::RampUp;
    mod.curvePoints.clear();  // No custom points -> use preset
    holder.update(mod);

    float val = CurveSnapshotHolder::evaluateCallback(0.5f, &holder);
    REQUIRE(val == Approx(0.5f));
}

// ============================================================================
// One-shot behavior
// ============================================================================

TEST_CASE("CurveSnapshotHolder - one-shot holds at end value", "[curve][oneshot]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    mod.oneShot = true;
    mod.curvePreset = CurvePreset::RampUp;
    mod.curvePoints.clear();
    holder.update(mod);

    // Simulate phase advancing: 0.0 -> 0.5 -> 0.9 -> wrap to 0.1
    float v1 = CurveSnapshotHolder::evaluateCallback(0.0f, &holder);
    REQUIRE(v1 == Approx(0.0f));

    float v2 = CurveSnapshotHolder::evaluateCallback(0.5f, &holder);
    REQUIRE(v2 == Approx(0.5f));

    float v3 = CurveSnapshotHolder::evaluateCallback(0.9f, &holder);
    REQUIRE(v3 == Approx(0.9f));

    // Phase wraps around -> should detect and hold at end value
    float v4 = CurveSnapshotHolder::evaluateCallback(0.1f, &holder);
    float endValue = CurveSnapshot::evaluatePreset(CurvePreset::RampUp, 0.999999f);
    REQUIRE(v4 == Approx(endValue));

    // Subsequent calls should still hold
    float v5 = CurveSnapshotHolder::evaluateCallback(0.3f, &holder);
    REQUIRE(v5 == Approx(endValue));
}

TEST_CASE("CurveSnapshotHolder - one-shot resetOneShot allows replay", "[curve][oneshot]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    mod.oneShot = true;
    mod.curvePreset = CurvePreset::RampUp;
    mod.curvePoints.clear();
    holder.update(mod);

    // Complete one cycle
    CurveSnapshotHolder::evaluateCallback(0.0f, &holder);
    CurveSnapshotHolder::evaluateCallback(0.9f, &holder);
    CurveSnapshotHolder::evaluateCallback(0.1f, &holder);  // Wrap -> completed

    float endValue = CurveSnapshot::evaluatePreset(CurvePreset::RampUp, 0.999999f);
    REQUIRE(CurveSnapshotHolder::evaluateCallback(0.5f, &holder) == Approx(endValue));

    // Reset one-shot
    holder.resetOneShot();

    // Should play normally again
    float v = CurveSnapshotHolder::evaluateCallback(0.5f, &holder);
    REQUIRE(v == Approx(0.5f));
}

TEST_CASE("CurveSnapshotHolder - one-shot with custom points", "[curve][oneshot]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    mod.oneShot = true;
    mod.curvePreset = CurvePreset::Custom;
    mod.curvePoints = {{0.0f, 0.0f, 0.0f}, {0.5f, 1.0f, 0.0f}, {1.0f, 0.5f, 0.0f}};
    holder.update(mod);

    // Advance through the cycle
    CurveSnapshotHolder::evaluateCallback(0.0f, &holder);
    CurveSnapshotHolder::evaluateCallback(0.9f, &holder);

    // Wrap -> hold at end
    float held = CurveSnapshotHolder::evaluateCallback(0.1f, &holder);

    // End value should be near point[2].value = 0.5 (evaluated at phase 0.999999)
    const CurveSnapshot* snap = holder.active.load();
    float expected = snap->evaluate(0.999999f);
    REQUIRE(held == Approx(expected));
}

TEST_CASE("CurveSnapshotHolder - disabling oneShot resets completed state", "[curve][oneshot]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    mod.oneShot = true;
    mod.curvePreset = CurvePreset::RampUp;
    holder.update(mod);

    // Complete the cycle
    CurveSnapshotHolder::evaluateCallback(0.0f, &holder);
    CurveSnapshotHolder::evaluateCallback(0.9f, &holder);
    CurveSnapshotHolder::evaluateCallback(0.1f, &holder);  // Completed

    // Turn off oneShot
    mod.oneShot = false;
    holder.update(mod);

    // Should loop normally now
    float v = CurveSnapshotHolder::evaluateCallback(0.5f, &holder);
    REQUIRE(v == Approx(0.5f));
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("CurveSnapshot - max points limit", "[curve][edge]") {
    CurveSnapshotHolder holder;

    ModInfo mod;
    // Add more than kMaxPoints
    for (int i = 0; i <= CurveSnapshot::kMaxPoints + 10; ++i) {
        float phase = static_cast<float>(i) / static_cast<float>(CurveSnapshot::kMaxPoints + 10);
        mod.curvePoints.push_back({phase, 0.5f, 0.0f});
    }

    holder.update(mod);
    const CurveSnapshot* snap = holder.active.load();
    REQUIRE(snap->count == CurveSnapshot::kMaxPoints);
}

TEST_CASE("CurveSnapshot::evaluate - phase at exact point positions", "[curve][edge]") {
    CurveSnapshot snap;
    snap.count = 3;
    snap.points[0] = {0.0f, 0.2f, 0.0f};
    snap.points[1] = {0.5f, 0.8f, 0.0f};
    snap.points[2] = {1.0f, 0.2f, 0.0f};

    // Evaluating exactly at point phases should return point values
    // (within floating point tolerance due to interpolation)
    REQUIRE(snap.evaluate(0.0f) == Approx(0.2f).margin(0.01f));
    REQUIRE(snap.evaluate(0.5f) == Approx(0.8f).margin(0.01f));
}
