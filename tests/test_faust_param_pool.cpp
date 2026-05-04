#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/audio/plugins/FaustParamPool.hpp"

using namespace magda::daw::audio;

namespace {

// Build a minimal HarvestedControl. Caller fills label / metadata as
// needed for the test. `zone` is left nullptr — we only check the slot
// table from these tests; real zones come in Phase 3.
HarvestedControl makeContinuous(const juce::String& label, float min = 0.0f, float max = 1.0f) {
    HarvestedControl h;
    h.kind = FaustParamSlot::Kind::Continuous;
    h.label = label;
    h.minValue = min;
    h.maxValue = max;
    h.stepValue = (max - min) / 100.0f;
    h.defaultValue = min;
    return h;
}

HarvestedControl makeBoolean(const juce::String& label) {
    HarvestedControl h;
    h.kind = FaustParamSlot::Kind::Boolean;
    h.label = label;
    h.minValue = 0.0f;
    h.maxValue = 1.0f;
    h.stepValue = 1.0f;
    return h;
}

}  // namespace

// ============================================================================
// Empty / encounter order
// ============================================================================

TEST_CASE("FaustParamPool - empty harvest leaves all slots inactive", "[faust][pool]") {
    FaustParamPool pool;
    auto report = pool.rebindFromHarvest({});
    REQUIRE(pool.activeCount() == 0);
    REQUIRE(report.activeBindings.empty());
    REQUIRE(report.diagnostics.empty());
}

TEST_CASE("FaustParamPool - encounter order fills sequential slots", "[faust][pool]") {
    FaustParamPool pool;
    auto report = pool.rebindFromHarvest({
        makeContinuous("Drive"),
        makeContinuous("Mix"),
        makeContinuous("Tone"),
    });
    REQUIRE(pool.activeCount() == 3);
    REQUIRE(pool.slot(0).active);
    REQUIRE(pool.slot(0).label == "Drive");
    REQUIRE(pool.slot(1).label == "Mix");
    REQUIRE(pool.slot(2).label == "Tone");
    REQUIRE_FALSE(pool.slot(3).active);
    REQUIRE(report.activeBindings.size() == 3);
    REQUIRE(report.activeBindings[0].slotIndex == 0);
}

// ============================================================================
// [idx:N] routing
// ============================================================================

TEST_CASE("FaustParamPool - idx-tagged controls claim their slot", "[faust][pool]") {
    FaustParamPool pool;

    auto a = makeContinuous("A");
    a.metadata.slotIndex = 5;
    auto b = makeContinuous("B");
    b.metadata.slotIndex = 2;

    auto report = pool.rebindFromHarvest({a, b});
    REQUIRE(pool.slot(5).active);
    REQUIRE(pool.slot(5).label == "A");
    REQUIRE(pool.slot(2).active);
    REQUIRE(pool.slot(2).label == "B");
    REQUIRE_FALSE(pool.slot(0).active);
    REQUIRE(report.diagnostics.empty());
}

TEST_CASE("FaustParamPool - idx pass runs before encounter pass", "[faust][pool]") {
    // [idx:0] should claim slot 0; the untagged control behind it then
    // skips to slot 1 instead of fighting for slot 0.
    FaustParamPool pool;
    auto a = makeContinuous("A");
    auto b = makeContinuous("B");
    b.metadata.slotIndex = 0;

    pool.rebindFromHarvest({a, b});
    REQUIRE(pool.slot(0).label == "B");
    REQUIRE(pool.slot(1).label == "A");
}

TEST_CASE("FaustParamPool - duplicate idx falls back to encounter order", "[faust][pool]") {
    FaustParamPool pool;
    auto a = makeContinuous("First");
    a.metadata.slotIndex = 4;
    auto b = makeContinuous("Second");
    b.metadata.slotIndex = 4;
    auto c = makeContinuous("Third");

    auto report = pool.rebindFromHarvest({a, b, c});
    REQUIRE(pool.slot(4).label == "First");
    // Second was deferred and lands at the first free slot (0); Third
    // also untagged, lands at slot 1.
    REQUIRE(pool.slot(0).label == "Second");
    REQUIRE(pool.slot(1).label == "Third");
    REQUIRE(report.diagnostics.size() >= 1);
}

TEST_CASE("FaustParamPool - out-of-range idx falls back with diagnostic", "[faust][pool]") {
    FaustParamPool pool;
    auto a = makeContinuous("OOB");
    a.metadata.slotIndex = 99;

    auto report = pool.rebindFromHarvest({a});
    REQUIRE(pool.slot(0).label == "OOB");
    REQUIRE(report.diagnostics.size() >= 1);
}

TEST_CASE("FaustParamPool - negative idx (other than -1) reported", "[faust][pool]") {
    FaustParamPool pool;
    auto a = makeContinuous("Neg");
    a.metadata.slotIndex = -3;

    auto report = pool.rebindFromHarvest({a});
    REQUIRE(pool.slot(0).label == "Neg");
    REQUIRE(report.diagnostics.size() == 1);
}

TEST_CASE("FaustParamPool - idx -1 (no tag) is silent", "[faust][pool]") {
    FaustParamPool pool;
    auto a = makeContinuous("Plain");
    REQUIRE(a.metadata.slotIndex == -1);

    auto report = pool.rebindFromHarvest({a});
    REQUIRE(report.diagnostics.empty());
}

// ============================================================================
// Pool overflow
// ============================================================================

TEST_CASE("FaustParamPool - >64 controls drop with diagnostic", "[faust][pool]") {
    FaustParamPool pool;
    std::vector<HarvestedControl> harvested;
    for (int i = 0; i < 70; ++i)
        harvested.push_back(makeContinuous("ctrl" + juce::String(i)));

    auto report = pool.rebindFromHarvest(harvested);
    REQUIRE(pool.activeCount() == FaustParamPool::kSize);
    REQUIRE(report.activeBindings.size() == FaustParamPool::kSize);
    REQUIRE(report.diagnostics.size() == 1);
    REQUIRE(report.diagnostics[0].contains("dropped"));
}

TEST_CASE("FaustParamPool - idx-tagged controls survive when untagged overflow", "[faust][pool]") {
    FaustParamPool pool;
    std::vector<HarvestedControl> harvested;
    auto pinned = makeContinuous("Pinned");
    pinned.metadata.slotIndex = 63;  // last slot
    harvested.push_back(pinned);
    for (int i = 0; i < 70; ++i)
        harvested.push_back(makeContinuous("c" + juce::String(i)));

    pool.rebindFromHarvest(harvested);
    // Pinned must still own slot 63 even though encounter-order
    // overflowed.
    REQUIRE(pool.slot(63).label == "Pinned");
}

// ============================================================================
// Kind / metadata propagation
// ============================================================================

TEST_CASE("FaustParamPool - boolean kind preserved", "[faust][pool]") {
    FaustParamPool pool;
    pool.rebindFromHarvest({makeBoolean("Toggle")});
    REQUIRE(pool.slot(0).kind == FaustParamSlot::Kind::Boolean);
}

TEST_CASE("FaustParamPool - menu metadata promotes Continuous to Discrete", "[faust][pool]") {
    FaustParamPool pool;
    auto h = makeContinuous("Mode", 0.0f, 2.0f);
    h.metadata.isMenuStyle = true;
    h.metadata.menuChoices = {{0.0f, "Off"}, {1.0f, "Low"}, {2.0f, "High"}};

    pool.rebindFromHarvest({h});
    REQUIRE(pool.slot(0).kind == FaustParamSlot::Kind::Discrete);
    REQUIRE(pool.slot(0).choices.size() == 3);
    REQUIRE(pool.slot(0).choices[2].second == "High");
}

TEST_CASE("FaustParamPool - log scale flag carries through", "[faust][pool]") {
    FaustParamPool pool;
    auto h = makeContinuous("Freq", 20.0f, 20000.0f);
    h.metadata.logScale = true;

    pool.rebindFromHarvest({h});
    REQUIRE(pool.slot(0).logScale);
    REQUIRE(pool.slot(0).minValue == 20.0f);
    REQUIRE(pool.slot(0).maxValue == 20000.0f);
}

TEST_CASE("FaustParamPool - unit propagates from metadata", "[faust][pool]") {
    FaustParamPool pool;
    auto h = makeContinuous("Cutoff");
    h.metadata.unit = "Hz";

    pool.rebindFromHarvest({h});
    REQUIRE(pool.slot(0).unit == "Hz");
}

// ============================================================================
// Active state across rebinds
// ============================================================================

TEST_CASE("FaustParamPool - rebind deactivates previously-active slots", "[faust][pool]") {
    FaustParamPool pool;
    pool.rebindFromHarvest({
        makeContinuous("A"),
        makeContinuous("B"),
        makeContinuous("C"),
    });
    REQUIRE(pool.activeCount() == 3);

    // New harvest with only one control — slots 1 and 2 should go
    // inactive even though they were filled before.
    pool.rebindFromHarvest({makeContinuous("Only")});
    REQUIRE(pool.activeCount() == 1);
    REQUIRE(pool.slot(0).label == "Only");
    REQUIRE_FALSE(pool.slot(1).active);
    REQUIRE_FALSE(pool.slot(2).active);
}

TEST_CASE("FaustParamPool - clearActive deactivates everything", "[faust][pool]") {
    FaustParamPool pool;
    pool.rebindFromHarvest({makeContinuous("A"), makeContinuous("B")});
    REQUIRE(pool.activeCount() == 2);
    pool.clearActive();
    REQUIRE(pool.activeCount() == 0);
}

TEST_CASE("FaustParamPool - discrete descriptor carries sorted real values", "[faust][pool]") {
    FaustParamPool pool;
    auto h = makeContinuous("Mode", 0.0f, 2.0f);
    h.metadata.isMenuStyle = true;
    h.metadata.menuChoices = {{2.0f, "High"}, {0.0f, "Off"}, {1.0f, "Low"}};

    auto report = pool.rebindFromHarvest({h});
    REQUIRE(report.activeBindings.size() == 1);
    const auto& d = report.activeBindings[0];
    REQUIRE(d.kind == FaustParamSlot::Kind::Discrete);
    REQUIRE(d.discreteValues.size() == 3);
    REQUIRE(d.discreteValues[0] == 0.0f);
    REQUIRE(d.discreteValues[1] == 1.0f);
    REQUIRE(d.discreteValues[2] == 2.0f);
}

TEST_CASE("FaustParamPool - non-discrete descriptors have empty discreteValues", "[faust][pool]") {
    FaustParamPool pool;
    pool.rebindFromHarvest({makeContinuous("Drive"), makeBoolean("On")});
    auto report = pool.rebindFromHarvest({makeContinuous("Drive"), makeBoolean("On")});
    REQUIRE(report.activeBindings[0].discreteValues.empty());
    REQUIRE(report.activeBindings[1].discreteValues.empty());
}

TEST_CASE("FaustParamPool - rebind report descriptors match slot data", "[faust][pool]") {
    FaustParamPool pool;
    auto h = makeContinuous("Freq", 20.0f, 20000.0f);
    h.metadata.logScale = true;

    auto report = pool.rebindFromHarvest({h});
    REQUIRE(report.activeBindings.size() == 1);
    const auto& d = report.activeBindings[0];
    REQUIRE(d.slotIndex == 0);
    REQUIRE(d.minValue == 20.0f);
    REQUIRE(d.maxValue == 20000.0f);
    REQUIRE(d.logScale);
}
