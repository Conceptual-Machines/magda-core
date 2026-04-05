#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/DeviceInfo.hpp"
#include "../magda/daw/core/RackInfo.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture
// ============================================================================

class MultiOutTestFixture {
  public:
    MultiOutTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~MultiOutTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }

    // Create an instrument track with a multi-out device that has 3 stereo output pairs
    struct MultiOutSetup {
        TrackId trackId;
        DeviceId deviceId;
    };

    MultiOutSetup createMultiOutTrack(const juce::String& name = "Inst") {
        auto trackId = tm().createTrack(name, TrackType::Audio);

        DeviceInfo instrument;
        instrument.name = "MultiOutSynth";
        instrument.format = PluginFormat::Internal;
        instrument.pluginId = "multisynth";
        instrument.isInstrument = true;
        instrument.multiOut.isMultiOut = true;
        instrument.multiOut.totalOutputChannels = 6;
        instrument.multiOut.outputPairs = {
            {0, "Main 1-2", false, INVALID_TRACK_ID, 1, 2},
            {1, "Out 3-4", false, INVALID_TRACK_ID, 3, 2},
            {2, "Out 5-6", false, INVALID_TRACK_ID, 5, 2},
        };

        auto deviceId = tm().addDeviceToTrack(trackId, instrument);
        return {trackId, deviceId};
    }
};

// ============================================================================
// Multi-Out Child Track Routing
// ============================================================================

TEST_CASE("Multi-out child tracks inherit parent output", "[multi_out][routing]") {
    MultiOutTestFixture fixture;

    auto [trackId, deviceId] = fixture.createMultiOutTrack();

    SECTION("activateMultiOutPair sets audioOutputDevice to master") {
        auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
        REQUIRE(childId != INVALID_TRACK_ID);

        auto* child = fixture.tm().getTrack(childId);
        REQUIRE(child != nullptr);
        REQUIRE(child->type == TrackType::MultiOut);
        REQUIRE(child->audioOutputDevice == "master");
    }

    SECTION("child inherits parent output when parent routes to group") {
        auto groupId = fixture.tm().createGroupTrack("My Group");
        fixture.tm().addTrackToGroup(trackId, groupId);

        // Verify parent now routes to the group
        auto* parent = fixture.tm().getTrack(trackId);
        REQUIRE(parent->audioOutputDevice == "track:" + juce::String(groupId));

        // Activate a multi-out pair — child should inherit parent's output
        auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
        REQUIRE(childId != INVALID_TRACK_ID);

        auto* child = fixture.tm().getTrack(childId);
        auto* parentAfter = fixture.tm().getTrack(trackId);
        REQUIRE(child->audioOutputDevice == parentAfter->audioOutputDevice);
    }

    SECTION("child has correct MultiOutTrackLink") {
        auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 2);
        REQUIRE(childId != INVALID_TRACK_ID);

        auto* child = fixture.tm().getTrack(childId);
        REQUIRE(child->multiOutLink.has_value());
        REQUIRE(child->multiOutLink->sourceTrackId == trackId);
        REQUIRE(child->multiOutLink->sourceDeviceId == deviceId);
        REQUIRE(child->multiOutLink->outputPairIndex == 2);
    }
}

// ============================================================================
// Group Routing Skips MultiOut Tracks
// ============================================================================

TEST_CASE("addTrackToGroup skips MultiOut track routing", "[multi_out][group]") {
    MultiOutTestFixture fixture;

    auto [trackId, deviceId] = fixture.createMultiOutTrack();

    // Activate a child pair first
    auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    auto* child = fixture.tm().getTrack(childId);
    REQUIRE(child->audioOutputDevice == "master");

    SECTION("adding MultiOut track to group does not change its routing") {
        auto groupId = fixture.tm().createGroupTrack("My Group");
        fixture.tm().addTrackToGroup(childId, groupId);

        child = fixture.tm().getTrack(childId);
        REQUIRE(child->audioOutputDevice == "master");
    }

    SECTION("adding parent to group does not affect child routing") {
        auto groupId = fixture.tm().createGroupTrack("My Group");
        fixture.tm().addTrackToGroup(trackId, groupId);

        // Parent routes to group
        auto* parent = fixture.tm().getTrack(trackId);
        REQUIRE(parent->audioOutputDevice == "track:" + juce::String(groupId));

        // Child still routes to master
        child = fixture.tm().getTrack(childId);
        REQUIRE(child->audioOutputDevice == "master");
    }
}

// ============================================================================
// ============================================================================
// Multi-Out Pair Activation / Deactivation
// ============================================================================

TEST_CASE("Multi-out pair activation and deactivation", "[multi_out][lifecycle]") {
    MultiOutTestFixture fixture;

    auto [trackId, deviceId] = fixture.createMultiOutTrack();

    SECTION("activating a pair marks it active and creates sibling track") {
        auto siblingId = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
        REQUIRE(siblingId != INVALID_TRACK_ID);

        auto* device = fixture.tm().getDevice(trackId, deviceId);
        REQUIRE(device->multiOut.outputPairs[1].active == true);
        REQUIRE(device->multiOut.outputPairs[1].trackId == siblingId);

        // Multi-out track should be a top-level sibling, not a child
        auto* sibling = fixture.tm().getTrack(siblingId);
        REQUIRE(sibling != nullptr);
        REQUIRE(sibling->parentId == INVALID_TRACK_ID);

        // Parent should NOT list it as a child
        auto* parent = fixture.tm().getTrack(trackId);
        REQUIRE(parent->childIds.empty());
    }

    SECTION("activating same pair twice returns existing track") {
        auto childId1 = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
        auto childId2 = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
        REQUIRE(childId1 == childId2);
    }

    SECTION("deactivating a pair removes the child track") {
        auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
        REQUIRE(childId != INVALID_TRACK_ID);

        fixture.tm().deactivateMultiOutPair(trackId, deviceId, 1);

        auto* device = fixture.tm().getDevice(trackId, deviceId);
        REQUIRE(device->multiOut.outputPairs[1].active == false);
        REQUIRE(device->multiOut.outputPairs[1].trackId == INVALID_TRACK_ID);

        // Child track should no longer exist
        REQUIRE(fixture.tm().getTrack(childId) == nullptr);
    }

    SECTION("invalid pair index returns INVALID_TRACK_ID") {
        auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 99);
        REQUIRE(childId == INVALID_TRACK_ID);
    }
}
