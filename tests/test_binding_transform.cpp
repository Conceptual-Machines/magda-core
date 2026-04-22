#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/controllers/BindingTransform.hpp"

using namespace magda;
using Catch::Approx;

// ============================================================================
// Helper: run the full pipeline (mode -> curve -> range)
// ============================================================================

static float pipeline(BindingMode mode, const BindingRange& range, int v, int vmax = 127) {
    auto out = applyMode(mode, {v, vmax});
    if (!out.isAbsolute)
        return out.value;  // relative: return raw delta (tests check delta separately)
    float curved = applyCurve(range.curve, out.value);
    return applyRange(range, curved);
}

// ============================================================================
// applyMode - Absolute
// ============================================================================

TEST_CASE("applyMode Absolute - endpoints and midpoint", "[controllers][transform]") {
    SECTION("zero") {
        auto r = applyMode(BindingMode::Absolute, {0, 127});
        REQUIRE(r.isAbsolute);
        REQUIRE(r.value == Approx(0.0f));
    }
    SECTION("max") {
        auto r = applyMode(BindingMode::Absolute, {127, 127});
        REQUIRE(r.isAbsolute);
        REQUIRE(r.value == Approx(1.0f));
    }
    SECTION("midpoint") {
        auto r = applyMode(BindingMode::Absolute, {64, 127});
        REQUIRE(r.isAbsolute);
        REQUIRE(r.value == Approx(64.0f / 127.0f));
    }
}

// ============================================================================
// applyCurve - all curves
// ============================================================================

TEST_CASE("applyCurve - endpoint invariants (all curves)", "[controllers][transform]") {
    for (auto curve :
         {BindingCurve::Linear, BindingCurve::Log, BindingCurve::Exp, BindingCurve::SCurve}) {
        REQUIRE(applyCurve(curve, 0.0f) == Approx(0.0f).epsilon(1e-5f));
        REQUIRE(applyCurve(curve, 1.0f) == Approx(1.0f).epsilon(1e-5f));
    }
}

TEST_CASE("applyCurve - monotonicity", "[controllers][transform]") {
    for (auto curve :
         {BindingCurve::Linear, BindingCurve::Log, BindingCurve::Exp, BindingCurve::SCurve}) {
        float prev = 0.0f;
        for (int i = 1; i <= 100; ++i) {
            float x = static_cast<float>(i) / 100.0f;
            float y = applyCurve(curve, x);
            REQUIRE(y >= prev - 1e-6f);
            prev = y;
        }
    }
}

TEST_CASE("applyCurve - Log and Exp are inverses", "[controllers][transform]") {
    for (int i = 0; i <= 10; ++i) {
        float x = static_cast<float>(i) / 10.0f;
        float logged = applyCurve(BindingCurve::Log, x);
        float roundtrip = applyCurve(BindingCurve::Exp, logged);
        REQUIRE(roundtrip == Approx(x).epsilon(1e-5f));
    }
}

// ============================================================================
// applyRange
// ============================================================================

TEST_CASE("applyRange - basic range mapping", "[controllers][transform]") {
    BindingRange range{0.2f, 0.8f, BindingCurve::Linear};
    REQUIRE(applyRange(range, 0.0f) == Approx(0.2f));
    REQUIRE(applyRange(range, 1.0f) == Approx(0.8f));
    REQUIRE(applyRange(range, 0.5f) == Approx(0.5f));
}

TEST_CASE("applyRange - inverted range", "[controllers][transform]") {
    BindingRange range{1.0f, 0.0f, BindingCurve::Linear};
    REQUIRE(applyRange(range, 0.0f) == Approx(1.0f));
    REQUIRE(applyRange(range, 1.0f) == Approx(0.0f));
}

// ============================================================================
// Full pipeline - Absolute + Linear + default range
// ============================================================================

TEST_CASE("pipeline Absolute Linear default range - endpoints", "[controllers][transform]") {
    BindingRange range;  // default: min=0, max=1, curve=Linear
    REQUIRE(pipeline(BindingMode::Absolute, range, 0) == Approx(0.0f));
    REQUIRE(pipeline(BindingMode::Absolute, range, 127) == Approx(1.0f));
}

TEST_CASE("pipeline Absolute Log - endpoints match", "[controllers][transform]") {
    BindingRange range{0.0f, 1.0f, BindingCurve::Log};
    REQUIRE(pipeline(BindingMode::Absolute, range, 0) == Approx(0.0f).epsilon(1e-5f));
    REQUIRE(pipeline(BindingMode::Absolute, range, 127) == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("pipeline Absolute Exp - endpoints match", "[controllers][transform]") {
    BindingRange range{0.0f, 1.0f, BindingCurve::Exp};
    REQUIRE(pipeline(BindingMode::Absolute, range, 0) == Approx(0.0f).epsilon(1e-5f));
    REQUIRE(pipeline(BindingMode::Absolute, range, 127) == Approx(1.0f).epsilon(1e-5f));
}

TEST_CASE("pipeline Absolute SCurve - endpoints match", "[controllers][transform]") {
    BindingRange range{0.0f, 1.0f, BindingCurve::SCurve};
    REQUIRE(pipeline(BindingMode::Absolute, range, 0) == Approx(0.0f).epsilon(1e-5f));
    REQUIRE(pipeline(BindingMode::Absolute, range, 127) == Approx(1.0f).epsilon(1e-5f));
}

// ============================================================================
// Relative modes - delta values
// ============================================================================

TEST_CASE("applyMode Relative2sComp", "[controllers][transform]") {
    // v=1  -> +1/64
    auto pos = applyMode(BindingMode::Relative2sComp, {1, 127});
    REQUIRE(!pos.isAbsolute);
    REQUIRE(pos.value == Approx(1.0f / 64.0f));

    // v=127 -> -1/64
    auto neg = applyMode(BindingMode::Relative2sComp, {127, 127});
    REQUIRE(!neg.isAbsolute);
    REQUIRE(neg.value == Approx(-1.0f / 64.0f));

    // v=64 -> -1.0
    auto big = applyMode(BindingMode::Relative2sComp, {64, 127});
    REQUIRE(!big.isAbsolute);
    REQUIRE(big.value == Approx(-1.0f));
}

TEST_CASE("applyMode RelativeSignMag", "[controllers][transform]") {
    // v=1 (sign=0, mag=1) -> +1/63
    auto pos = applyMode(BindingMode::RelativeSignMag, {1, 127});
    REQUIRE(!pos.isAbsolute);
    REQUIRE(pos.value == Approx(1.0f / 63.0f));

    // v=0x41 (sign=1, mag=1) -> -1/63
    auto neg = applyMode(BindingMode::RelativeSignMag, {0x41, 127});
    REQUIRE(!neg.isAbsolute);
    REQUIRE(neg.value == Approx(-1.0f / 63.0f));

    // v=0x3F (sign=0, mag=63) -> +1.0
    auto full_pos = applyMode(BindingMode::RelativeSignMag, {0x3F, 127});
    REQUIRE(!full_pos.isAbsolute);
    REQUIRE(full_pos.value == Approx(1.0f));

    // v=0x7F (sign=1, mag=63) -> -1.0
    auto full_neg = applyMode(BindingMode::RelativeSignMag, {0x7F, 127});
    REQUIRE(!full_neg.isAbsolute);
    REQUIRE(full_neg.value == Approx(-1.0f));
}

TEST_CASE("applyMode RelativeBinOff", "[controllers][transform]") {
    // v=64 -> 0 (no change)
    auto zero = applyMode(BindingMode::RelativeBinOff, {64, 127});
    REQUIRE(!zero.isAbsolute);
    REQUIRE(zero.value == Approx(0.0f));

    // v=65 -> +1/64
    auto pos = applyMode(BindingMode::RelativeBinOff, {65, 127});
    REQUIRE(!pos.isAbsolute);
    REQUIRE(pos.value == Approx(1.0f / 64.0f));

    // v=63 -> -1/64
    auto neg = applyMode(BindingMode::RelativeBinOff, {63, 127});
    REQUIRE(!neg.isAbsolute);
    REQUIRE(neg.value == Approx(-1.0f / 64.0f));
}

// ============================================================================
// Toggle
// ============================================================================

TEST_CASE("applyToggle - rising edge detection", "[controllers][transform]") {
    ToggleState state;

    // First high message: toggles to true
    float v1 = applyToggle(127, state);
    REQUIRE(state.on == true);
    REQUIRE(v1 == Approx(1.0f));

    // Second high message (no drop below 64): no re-trigger
    float v2 = applyToggle(127, state);
    REQUIRE(state.on == true);
    REQUIRE(v2 == Approx(1.0f));

    // Drop below 64
    applyToggle(0, state);
    REQUIRE(state.on == true);  // state unchanged on low

    // Rising edge again: toggles to false
    float v3 = applyToggle(100, state);
    REQUIRE(state.on == false);
    REQUIRE(v3 == Approx(0.0f));
}

TEST_CASE("applyToggle - no re-trigger on consecutive highs", "[controllers][transform]") {
    ToggleState state;

    // Send 127 twice
    applyToggle(127, state);
    bool stateAfterFirst = state.on;
    applyToggle(127, state);
    // State should not change on second 127
    REQUIRE(state.on == stateAfterFirst);
}

TEST_CASE("applyToggle - requires drop below 64 to re-arm", "[controllers][transform]") {
    ToggleState state;

    applyToggle(127, state);  // rising edge: state.on = true
    applyToggle(100, state);  // still high: no change
    applyToggle(63, state);   // drop: re-arm
    applyToggle(64, state);   // >= 64: rising edge again: state.on = false
    REQUIRE(state.on == false);
}
