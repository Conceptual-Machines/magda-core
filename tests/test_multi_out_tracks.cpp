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
        auto trackId = tm().createTrack(name, TrackType::Media);

        DeviceInfo instrument;
        instrument.name = "MultiOutSynth";
        instrument.format = PluginFormat::Internal;
        instrument.pluginId = "multisynth";
        instrument.isInstrument = true;
        instrument.multiOut.isMultiOut = true;
        instrument.multiOut.totalOutputChannels = 6;
        instrument.multiOut.outputPairs = {
            {0, "Main 1-2", 1, 2},
            {1, "Out 3-4", 3, 2},
            {2, "Out 5-6", 5, 2},
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

TEST_CASE("Multi-out tracks route through groups", "[multi_out][group]") {
    MultiOutTestFixture fixture;

    auto [trackId, deviceId] = fixture.createMultiOutTrack();

    // Activate a child pair first
    auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    auto* child = fixture.tm().getTrack(childId);
    REQUIRE(child->audioOutputDevice == "master");

    SECTION("adding MultiOut track to group routes it to the group") {
        auto groupId = fixture.tm().createGroupTrack("My Group");
        fixture.tm().addTrackToGroup(childId, groupId);

        child = fixture.tm().getTrack(childId);
        REQUIRE(child->audioOutputDevice == "track:" + juce::String(groupId));
    }

    SECTION("adding parent to group updates existing child routing") {
        auto groupId = fixture.tm().createGroupTrack("My Group");
        fixture.tm().addTrackToGroup(trackId, groupId);

        // Parent routes to group
        auto* parent = fixture.tm().getTrack(trackId);
        REQUIRE(parent->audioOutputDevice == "track:" + juce::String(groupId));

        // Ungrouped multi-out child follows the source track output.
        child = fixture.tm().getTrack(childId);
        REQUIRE(child->audioOutputDevice == parent->audioOutputDevice);
    }

    SECTION("removing parent from group returns existing child routing to master") {
        auto groupId = fixture.tm().createGroupTrack("My Group");
        fixture.tm().addTrackToGroup(trackId, groupId);
        fixture.tm().removeTrackFromGroup(trackId);

        auto* parent = fixture.tm().getTrack(trackId);
        REQUIRE(parent->audioOutputDevice == "master");

        child = fixture.tm().getTrack(childId);
        REQUIRE(child->audioOutputDevice == "master");
    }

    SECTION("explicit child group routing wins over parent group routing") {
        auto parentGroupId = fixture.tm().createGroupTrack("Parent Group");
        auto childGroupId = fixture.tm().createGroupTrack("Child Group");

        fixture.tm().addTrackToGroup(childId, childGroupId);
        fixture.tm().addTrackToGroup(trackId, parentGroupId);

        child = fixture.tm().getTrack(childId);
        REQUIRE(child->audioOutputDevice == "track:" + juce::String(childGroupId));
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

        // Derived from the child track's own link, not from a flag the device
        // carries (#2220).
        REQUIRE(fixture.tm().multiOutPairIsActive(trackId, deviceId, 1));
        REQUIRE(fixture.tm().multiOutChildTrack(trackId, deviceId, 1) == siblingId);

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

        REQUIRE_FALSE(fixture.tm().multiOutPairIsActive(trackId, deviceId, 1));
        REQUIRE(fixture.tm().multiOutChildTrack(trackId, deviceId, 1) == INVALID_TRACK_ID);

        // Child track should no longer exist
        REQUIRE(fixture.tm().getTrack(childId) == nullptr);
    }

    SECTION("invalid pair index returns INVALID_TRACK_ID") {
        auto childId = fixture.tm().activateMultiOutPair(trackId, deviceId, 99);
        REQUIRE(childId == INVALID_TRACK_ID);
    }
}

// ============================================================================
// Ownership is the child track's, and only the child track's (#2220)
//
// `active` and `trackId` used to sit on the device's output pair as well as on
// the child track's `multiOutLink`. Two records of one fact, and a `DeviceInfo`
// travels with duplication, preset import and paste, so a copy arrived claiming
// the original's child track. These pin that there is one record now and that
// every transition either keeps it consistent or leaves nothing behind.
// ============================================================================

TEST_CASE("Duplicating a track does not duplicate child-track ownership",
          "[multi_out][lifecycle][ownership]") {
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();
    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    const auto copyId = tm.duplicateTrack(trackId);
    REQUIRE(copyId != INVALID_TRACK_ID);

    const auto* copy = tm.getTrack(copyId);
    REQUIRE(copy != nullptr);
    REQUIRE(copy->chain.fxChainElements.size() == 1);
    const auto copyDeviceId = getDevice(copy->chain.fxChainElements[0]).id;
    REQUIRE(copyDeviceId != deviceId);

    // The declarative description came with the copy.
    const auto* copyDevice = tm.getDevice(copyId, copyDeviceId);
    REQUIRE(copyDevice != nullptr);
    CHECK(copyDevice->multiOut.isMultiOut);
    CHECK(copyDevice->multiOut.outputPairs.size() == 3);
    CHECK(copyDevice->multiOut.outputPairs[1].name == "Out 3-4");
    CHECK(copyDevice->multiOut.outputPairs[1].firstPin == 3);

    // The ownership did not.
    CHECK_FALSE(tm.multiOutPairIsActive(copyId, copyDeviceId, 1));
    CHECK(tm.multiOutChildTrack(copyId, copyDeviceId, 1) == INVALID_TRACK_ID);

    // And the original still drives its own, unmoved.
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);
    const auto* child = tm.getTrack(childId);
    REQUIRE(child != nullptr);
    REQUIRE(child->multiOutLink.has_value());
    CHECK(child->multiOutLink->sourceTrackId == trackId);
    CHECK(child->multiOutLink->sourceDeviceId == deviceId);
}

TEST_CASE("Removing the child track ends the assignment", "[multi_out][lifecycle][ownership]") {
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();
    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    // Removed as a track, not through deactivateMultiOutPair: the record went
    // with it, so there is no second copy left claiming a track that is gone.
    tm.deleteTrack(childId);

    CHECK(tm.getTrack(childId) == nullptr);
    CHECK_FALSE(tm.multiOutPairIsActive(trackId, deviceId, 1));
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == INVALID_TRACK_ID);

    // And the pair can be opened again, onto a track of its own.
    const auto reopened = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(reopened != INVALID_TRACK_ID);
    CHECK(reopened != childId);
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == reopened);
}

TEST_CASE("Two pairs of one device own two different tracks", "[multi_out][lifecycle][ownership]") {
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();
    const auto firstId = tm.activateMultiOutPair(trackId, deviceId, 1);
    const auto secondId = tm.activateMultiOutPair(trackId, deviceId, 2);
    REQUIRE(firstId != INVALID_TRACK_ID);
    REQUIRE(secondId != INVALID_TRACK_ID);
    REQUIRE(firstId != secondId);

    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == firstId);
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 2) == secondId);

    // Closing one leaves the other alone.
    tm.deactivateMultiOutPair(trackId, deviceId, 1);
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == INVALID_TRACK_ID);
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 2) == secondId);
}

TEST_CASE("Two devices with the same pair index own different tracks",
          "[multi_out][lifecycle][ownership]") {
    // The record is keyed by source track, source device and pair, so two grids
    // opening the same numbered bus cannot be confused for one another.
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackA, deviceA] = fixture.createMultiOutTrack("A");
    auto [trackB, deviceB] = fixture.createMultiOutTrack("B");

    const auto childA = tm.activateMultiOutPair(trackA, deviceA, 1);
    const auto childB = tm.activateMultiOutPair(trackB, deviceB, 1);
    REQUIRE(childA != INVALID_TRACK_ID);
    REQUIRE(childB != INVALID_TRACK_ID);
    REQUIRE(childA != childB);

    CHECK(tm.multiOutChildTrack(trackA, deviceA, 1) == childA);
    CHECK(tm.multiOutChildTrack(trackB, deviceB, 1) == childB);

    // And neither answers for the other's device.
    CHECK(tm.multiOutChildTrack(trackA, deviceB, 1) == INVALID_TRACK_ID);
    CHECK(tm.multiOutChildTrack(trackB, deviceA, 1) == INVALID_TRACK_ID);
}

TEST_CASE("An unopened pair owns nothing", "[multi_out][lifecycle][ownership]") {
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();

    for (int pair = 0; pair < 3; ++pair)
        CHECK_FALSE(tm.multiOutPairIsActive(trackId, deviceId, pair));

    // And an address that names nothing is answered, not guessed at.
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 99) == INVALID_TRACK_ID);
    CHECK(tm.multiOutChildTrack(trackId, deviceId, -1) == INVALID_TRACK_ID);
    CHECK(tm.multiOutChildTrack(INVALID_TRACK_ID, deviceId, 1) == INVALID_TRACK_ID);
    CHECK(tm.multiOutChildTrack(trackId, INVALID_DEVICE_ID, 1) == INVALID_TRACK_ID);
}

TEST_CASE("A device driving child tracks cannot change placement",
          "[multi_out][lifecycle][ownership]") {
    // The child track's link names the track the device stands on, so a move
    // would strand it. Refused before anything is mutated; closing the pairs is
    // the documented way to move such a device (#2220).
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();
    const auto otherTrackId = tm.createTrack("Other", TrackType::Media);
    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    const auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);
    const auto elsewhere = ChainNodePath::trackLevel(otherTrackId);

    CHECK_FALSE(tm.moveChainElement(devicePath, elsewhere, 0));

    // Refused means unchanged: the device is where it was and still drives its
    // child, which still names the track it was made against.
    CHECK(tm.findDevicePath(deviceId) == devicePath);
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);
    const auto* child = tm.getTrack(childId);
    REQUIRE(child != nullptr);
    REQUIRE(child->multiOutLink.has_value());
    CHECK(child->multiOutLink->sourceTrackId == trackId);

    // Closing the pair lets it move.
    tm.deactivateMultiOutPair(trackId, deviceId, 1);
    REQUIRE_FALSE(tm.multiOutPairIsActive(trackId, deviceId, 1));
    CHECK(tm.moveChainElement(devicePath, elsewhere, 0));
    CHECK(tm.findDevicePath(deviceId) == ChainNodePath::topLevelDevice(otherTrackId, deviceId));
}

TEST_CASE("An active multi-out device reorders inside its own nested chain",
          "[multi_out][lifecycle][ownership]") {
    // Reordering is not a placement change. A nested multi-out device moving to
    // another index in the chain it already lives in changes neither the track
    // nor the device id its child track is keyed on, so the guard above must not
    // catch it (#2220).
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    const auto trackId = tm.createTrack("Inst", TrackType::Media);
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto chainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

    DeviceInfo instrument;
    instrument.name = "MultiOutSynth";
    instrument.format = PluginFormat::Internal;
    instrument.pluginId = "multisynth";
    instrument.isInstrument = true;
    instrument.multiOut.isMultiOut = true;
    instrument.multiOut.totalOutputChannels = 4;
    instrument.multiOut.outputPairs = {{0, "Main 1-2", 1, 2}, {1, "Out 3-4", 3, 2}};

    const auto deviceId = tm.addDeviceToChainByPath(chainPath, instrument);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    DeviceInfo neighbour;
    neighbour.name = "Filter";
    neighbour.format = PluginFormat::Internal;
    neighbour.pluginId = "magdafilter";
    const auto neighbourId = tm.addDeviceToChainByPath(chainPath, neighbour);
    REQUIRE(neighbourId != INVALID_DEVICE_ID);

    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);
    REQUIRE(tm.multiOutPairIsActive(trackId, deviceId, 1));

    // Same container, new index: allowed, and ownership is untouched.
    REQUIRE(tm.moveChainElement(chainPath.withDevice(deviceId), chainPath, 2));

    const auto* chain = tm.getChainByPath(chainPath);
    REQUIRE(chain != nullptr);
    REQUIRE(chain->elements.size() == 2);
    CHECK(getDevice(chain->elements[0]).id == neighbourId);
    CHECK(getDevice(chain->elements[1]).id == deviceId);

    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);
    const auto* child = tm.getTrack(childId);
    REQUIRE(child != nullptr);
    REQUIRE(child->multiOutLink.has_value());
    CHECK(child->multiOutLink->sourceTrackId == trackId);
    CHECK(child->multiOutLink->sourceDeviceId == deviceId);

    // Leaving the container is still refused.
    CHECK_FALSE(
        tm.moveChainElement(chainPath.withDevice(deviceId), ChainNodePath::trackLevel(trackId), 0));
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);
}

TEST_CASE("Wrapping a device that drives child tracks is refused",
          "[multi_out][lifecycle][ownership][placement]") {
    // Wrapping takes the device off the top level, where the output instance
    // that carries a bus is made, so it strands the child track exactly as a
    // move would. Wrap used to ask only whether a pad was on a bus, which a
    // plain multi-out instrument never trips (#2221).
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();
    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    CHECK(tm.wrapDeviceInRack(trackId, deviceId) == INVALID_RACK_ID);

    // Refused means unchanged.
    CHECK(tm.findDevicePath(deviceId) == ChainNodePath::topLevelDevice(trackId, deviceId));
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);

    // Closing the pair lets it wrap.
    tm.deactivateMultiOutPair(trackId, deviceId, 1);
    CHECK(tm.wrapDeviceInRack(trackId, deviceId) != INVALID_RACK_ID);
}

TEST_CASE("Wrapping a selection containing such a device is refused",
          "[multi_out][lifecycle][ownership][placement]") {
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    auto [trackId, deviceId] = fixture.createMultiOutTrack();

    DeviceInfo neighbour;
    neighbour.name = "Delay";
    neighbour.pluginId = "delay";
    neighbour.format = PluginFormat::Internal;
    const auto neighbourId = tm.addDeviceToTrack(trackId, neighbour);
    REQUIRE(neighbourId != INVALID_DEVICE_ID);

    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    const std::vector<ChainNodePath> selection{ChainNodePath::topLevelDevice(trackId, deviceId),
                                               ChainNodePath::topLevelDevice(trackId, neighbourId)};

    CHECK(tm.wrapChainElementsInRack(selection, "Rack") == INVALID_RACK_ID);

    // Neither device moved, and the ownership stands.
    CHECK(tm.findDevicePath(deviceId) == ChainNodePath::topLevelDevice(trackId, deviceId));
    CHECK(tm.findDevicePath(neighbourId) == ChainNodePath::topLevelDevice(trackId, neighbourId));
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);
}

TEST_CASE("An instrument cannot be pasted onto a track that cannot host one",
          "[multi_out][placement]") {
    // insertChainElementsByPath asked nothing before, so paste could put an
    // instrument where the move path refuses to put one (#2221).
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    const auto auxId = tm.createTrack("Aux", TrackType::Aux);
    REQUIRE(auxId != INVALID_TRACK_ID);
    const auto* aux = tm.getTrack(auxId);
    REQUIRE(aux != nullptr);
    REQUIRE_FALSE(aux->canHostInstrument());

    DeviceInfo instrument;
    instrument.name = "MultiOutSynth";
    instrument.pluginId = "multisynth";
    instrument.format = PluginFormat::Internal;
    instrument.isInstrument = true;

    std::vector<ChainElement> pasted;
    pasted.push_back(makeDeviceElement(instrument));

    CHECK_FALSE(tm.insertChainElementsByPath(ChainNodePath::trackLevel(auxId), std::move(pasted), 0,
                                             /*reassignIds=*/true));
    CHECK(tm.getTrack(auxId)->chain.fxChainElements.empty());

    // An effect is fine.
    DeviceInfo effect;
    effect.name = "Delay";
    effect.pluginId = "delay";
    effect.format = PluginFormat::Internal;

    std::vector<ChainElement> effectPaste;
    effectPaste.push_back(makeDeviceElement(effect));
    CHECK(tm.insertChainElementsByPath(ChainNodePath::trackLevel(auxId), std::move(effectPaste), 0,
                                       /*reassignIds=*/true));
    CHECK(tm.getTrack(auxId)->chain.fxChainElements.size() == 1);
}

TEST_CASE("Wrapping a nested device that drives child tracks is refused",
          "[multi_out][lifecycle][ownership][placement]") {
    // `wrapDeviceInRackByPath()` delegates its top-level case to
    // `wrapDeviceInRack()`, but its nested branch extracted and wrapped the
    // device on its own and so went round the placement boundary (#2221).
    MultiOutTestFixture fixture;
    auto& tm = fixture.tm();

    const auto trackId = tm.createTrack("Inst", TrackType::Media);
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto chainId = tm.addChainToRack(ChainNodePath::rack(trackId, rackId));
    const auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

    DeviceInfo instrument;
    instrument.name = "MultiOutSynth";
    instrument.format = PluginFormat::Internal;
    instrument.pluginId = "multisynth";
    instrument.isInstrument = true;
    instrument.multiOut.isMultiOut = true;
    instrument.multiOut.totalOutputChannels = 4;
    instrument.multiOut.outputPairs = {{0, "Main 1-2", 1, 2}, {1, "Out 3-4", 3, 2}};

    const auto deviceId = tm.addDeviceToChainByPath(chainPath, instrument);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const auto childId = tm.activateMultiOutPair(trackId, deviceId, 1);
    REQUIRE(childId != INVALID_TRACK_ID);

    const auto devicePath = chainPath.withDevice(deviceId);
    CHECK(tm.wrapDeviceInRackByPath(devicePath, "Wrapper") == INVALID_RACK_ID);

    // Refused means unchanged: still directly in its chain, still driving its
    // child track.
    CHECK(tm.findDevicePath(deviceId) == devicePath);
    const auto* chain = tm.getChainByPath(chainPath);
    REQUIRE(chain != nullptr);
    REQUIRE(chain->elements.size() == 1);
    CHECK(isDevice(chain->elements[0]));
    CHECK(tm.multiOutChildTrack(trackId, deviceId, 1) == childId);

    // Closing the pair lets it wrap.
    tm.deactivateMultiOutPair(trackId, deviceId, 1);
    CHECK(tm.wrapDeviceInRackByPath(devicePath, "Wrapper") != INVALID_RACK_ID);
}
