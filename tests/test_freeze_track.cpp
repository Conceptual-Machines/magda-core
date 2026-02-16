#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Helper: reset state before each test
// ============================================================================
static void resetState() {
    TrackManager::getInstance().clearAllTracks();
}

// Helper: create a DeviceInfo
static DeviceInfo makeDevice(const char* name, bool instrument = false) {
    DeviceInfo d;
    d.name = name;
    d.isInstrument = instrument;
    return d;
}

// Helper: create a RackInfo with one chain containing given elements
static std::unique_ptr<RackInfo> makeRack(std::vector<ChainElement> elements) {
    auto rack = std::make_unique<RackInfo>();
    ChainInfo chain;
    chain.elements = std::move(elements);
    rack->chains.push_back(std::move(chain));
    return rack;
}

// ============================================================================
// TrackInfo::hasInstrument()
// ============================================================================

TEST_CASE("TrackInfo::hasInstrument - empty chain", "[track][freeze]") {
    TrackInfo track;
    REQUIRE_FALSE(track.hasInstrument());
}

TEST_CASE("TrackInfo::hasInstrument - effects only", "[track][freeze]") {
    TrackInfo track;
    track.chainElements.push_back(makeDevice("EQ"));
    track.chainElements.push_back(makeDevice("Compressor"));
    REQUIRE_FALSE(track.hasInstrument());
}

TEST_CASE("TrackInfo::hasInstrument - top-level instrument", "[track][freeze]") {
    TrackInfo track;
    track.chainElements.push_back(makeDevice("Serum", true));
    track.chainElements.push_back(makeDevice("Reverb"));
    REQUIRE(track.hasInstrument());
}

TEST_CASE("TrackInfo::hasInstrument - instrument inside rack", "[track][freeze]") {
    TrackInfo track;
    std::vector<ChainElement> rackElements;
    rackElements.push_back(makeDevice("Vital", true));
    rackElements.push_back(makeDevice("Delay"));
    track.chainElements.push_back(makeRack(std::move(rackElements)));
    REQUIRE(track.hasInstrument());
}

TEST_CASE("TrackInfo::hasInstrument - rack with effects only", "[track][freeze]") {
    TrackInfo track;
    std::vector<ChainElement> rackElements;
    rackElements.push_back(makeDevice("EQ"));
    rackElements.push_back(makeDevice("Limiter"));
    track.chainElements.push_back(makeRack(std::move(rackElements)));
    REQUIRE_FALSE(track.hasInstrument());
}

TEST_CASE("TrackInfo::hasInstrument - mixed chain with rack", "[track][freeze]") {
    TrackInfo track;
    // Top-level effect
    track.chainElements.push_back(makeDevice("EQ"));
    // Rack with no instrument
    std::vector<ChainElement> rackElements;
    rackElements.push_back(makeDevice("Compressor"));
    track.chainElements.push_back(makeRack(std::move(rackElements)));
    // Top-level instrument
    track.chainElements.push_back(makeDevice("Massive X", true));
    REQUIRE(track.hasInstrument());
}

// ============================================================================
// TrackManager::setTrackFrozen()
// ============================================================================

TEST_CASE("TrackManager::setTrackFrozen - sets frozen state", "[track][freeze]") {
    resetState();
    TrackId id = TrackManager::getInstance().createTrack("Test Track");
    auto* track = TrackManager::getInstance().getTrack(id);
    REQUIRE(track != nullptr);
    REQUIRE_FALSE(track->frozen);

    TrackManager::getInstance().setTrackFrozen(id, true);
    REQUIRE(track->frozen);

    TrackManager::getInstance().setTrackFrozen(id, false);
    REQUIRE_FALSE(track->frozen);
}

TEST_CASE("TrackManager::setTrackFrozen - invalid track is no-op", "[track][freeze]") {
    resetState();
    // Should not crash
    TrackManager::getInstance().setTrackFrozen(9999, true);
}

// ============================================================================
// TrackInfo frozen state preserved through copy
// ============================================================================

TEST_CASE("TrackInfo copy preserves frozen state", "[track][freeze]") {
    TrackInfo original;
    original.frozen = true;
    original.name = "Frozen Track";
    original.chainElements.push_back(makeDevice("Synth", true));

    SECTION("Copy constructor") {
        TrackInfo copy(original);
        REQUIRE(copy.frozen);
        REQUIRE(copy.name == "Frozen Track");
        REQUIRE(copy.hasInstrument());
    }

    SECTION("Copy assignment") {
        TrackInfo copy;
        REQUIRE_FALSE(copy.frozen);
        copy = original;
        REQUIRE(copy.frozen);
        REQUIRE(copy.name == "Frozen Track");
        REQUIRE(copy.hasInstrument());
    }
}

// ============================================================================
// Frozen track defaults
// ============================================================================

TEST_CASE("TrackInfo frozen defaults to false", "[track][freeze]") {
    TrackInfo track;
    REQUIRE_FALSE(track.frozen);
}
