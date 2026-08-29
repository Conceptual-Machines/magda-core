#include <catch2/catch_test_macros.hpp>
#include <string>

#include "magda/daw/core/ChainWalk.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

// The one descent, and the order it goes in (#2204).
//
// A dozen walks each implemented this, and a consumer that swapped to the
// shared one would change behaviour if the order moved: the gain-staging
// cascade reads its devices in signal order, and the bypass sweep announces
// the paths it touched in the order it touched them. So the order is asserted
// rather than left to whatever the recursion happens to do.

namespace {

DeviceInfo effect(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "delay";
    device.format = PluginFormat::Internal;
    return device;
}

DeviceInfo drumGrid(const juce::String& name) {
    DeviceInfo device;
    device.name = name;
    device.pluginId = "drumgrid";
    device.format = PluginFormat::Internal;
    device.isInstrument = true;
    device.deviceType = DeviceType::Instrument;
    return device;
}

/// The names the walk visited, in order, comma-separated.
std::string order(const std::vector<juce::String>& names) {
    juce::String joined;
    for (const auto& name : names)
        joined += (joined.isEmpty() ? "" : ",") + name;
    return joined.toStdString();
}

}  // namespace

TEST_CASE("The device walk is depth-first in element order", "[chain-walk]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    // Track: A, Rack[ chain1: B, Rack[ chain: C ]; chain2: D ], E
    const auto trackId = tm.createTrack("Track");
    tm.addDeviceToTrack(trackId, effect("A"));
    const auto rackId = tm.addRackToTrack(trackId, "Outer");
    tm.addDeviceToTrack(trackId, effect("E"));

    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto chainOne = tm.addChainToRack(rackPath);
    const auto chainTwo = tm.addChainToRack(rackPath);
    tm.addDeviceToChainByPath(rackPath.withChain(chainOne), effect("B"));
    const auto innerId = tm.addRackToChainByPath(rackPath.withChain(chainOne), "Inner");
    const auto innerPath = rackPath.withChain(chainOne).withRack(innerId);
    tm.addDeviceToChainByPath(innerPath.withChain(tm.addChainToRack(innerPath)), effect("C"));
    tm.addDeviceToChainByPath(rackPath.withChain(chainTwo), effect("D"));

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    std::vector<juce::String> visited;
    chain_walk::forEachDevice(track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
                              chain_walk::Pads::Skip,
                              [&visited](const DeviceInfo& device, const ChainNodePath&) {
                                  visited.push_back(device.name);
                              });

    // Depth-first and pre-order: the rack's contents come between the elements
    // standing either side of it, and the nested rack's device comes before the
    // outer rack's second chain.
    CHECK(order(visited) == "A,B,C,D,E");

    tm.clearAllTracks();
}

TEST_CASE("The device walk spells a top-level device the way everything else stores it",
          "[chain-walk]") {
    // The reason the walk builds the path rather than the caller: at the top
    // level a device's id lives in `topLevelDeviceId` rather than in a step, so
    // `withDevice()` would name the same device with a path that compares equal
    // to nothing the model holds.
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    const auto topId = tm.addDeviceToTrack(trackId, effect("Top"));
    const auto rackId = tm.addRackToTrack(trackId, "Rack");
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto chainId = tm.addChainToRack(rackPath);
    const auto nestedId = tm.addDeviceToChainByPath(rackPath.withChain(chainId), effect("Nested"));

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    std::vector<ChainNodePath> paths;
    chain_walk::forEachDevice(
        track->chain.fxChainElements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Skip,
        [&paths](const DeviceInfo&, const ChainNodePath& path) { paths.push_back(path); });

    REQUIRE(paths.size() == 2);
    CHECK(paths[0] == ChainNodePath::topLevelDevice(trackId, topId));
    CHECK(paths[1] == rackPath.withChain(chainId).withDevice(nestedId));

    // The other spelling names the same device and is a different path, which
    // is the trap the walk exists to close.
    CHECK_FALSE(paths[0] == ChainNodePath::trackLevel(trackId).withDevice(topId));

    tm.clearAllTracks();
}

TEST_CASE("A device's pads are walked after it, and only when asked for", "[chain-walk]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGrid("Grid"));
    tm.addDeviceToTrack(trackId, effect("After"));

    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    REQUIRE(padChainId != INVALID_CHAIN_ID);
    REQUIRE(tm.addDeviceToPad(gridPath, padChainId, effect("OnPad")) != INVALID_DEVICE_ID);

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    auto walk = [&](chain_walk::Pads pads) {
        std::vector<juce::String> visited;
        chain_walk::forEachDevice(track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
                                  pads, [&visited](const DeviceInfo& device, const ChainNodePath&) {
                                      visited.push_back(device.name);
                                  });
        return order(visited);
    };

    // A device is not a leaf when it owns pads, and its pads come after it and
    // before whatever stands next to it, like a rack's chains do.
    CHECK(walk(chain_walk::Pads::Enter) == "Grid,OnPad,After");
    CHECK(walk(chain_walk::Pads::Skip) == "Grid,After");

    tm.clearAllTracks();
}

TEST_CASE("A pad device is addressed off the track by its grid's id", "[chain-walk]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGrid("Grid"));
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    const auto padDeviceId = tm.addDeviceToPad(gridPath, padChainId, effect("OnPad"));
    REQUIRE(padDeviceId != INVALID_DEVICE_ID);

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    std::vector<ChainNodePath> paths;
    chain_walk::forEachDevice(
        track->chain.fxChainElements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Enter,
        [&paths](const DeviceInfo&, const ChainNodePath& path) { paths.push_back(path); });

    REQUIRE(paths.size() == 2);
    CHECK(paths[1] == ChainNodePath::padChain(trackId, gridId, padChainId).withDevice(padDeviceId));

    tm.clearAllTracks();
}

TEST_CASE("A pad rack is a rack the walk yields", "[chain-walk]") {
    // A pad rack is a RackInfo hanging off a device rather than sitting in the
    // chain as an element, so a walk that only descended into its chains would
    // hand a rack visitor every rack but this one. It carries mute, solo and a
    // fader like any other, which is what PlanValues looks it up for, and a
    // consumer migrating to the walk would have lost it silently (#2204).
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    tm.addRackToTrack(trackId, "Ordinary");
    const auto gridId = tm.addDeviceToTrack(trackId, drumGrid("Grid"));
    const auto gridPath = ChainNodePath::topLevelDevice(trackId, gridId);
    REQUIRE(tm.ensurePad(gridPath, 0) != INVALID_CHAIN_ID);

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    auto walk = [&](chain_walk::Pads pads) {
        std::vector<juce::String> visited;
        chain_walk::forEachRack(track->chain.fxChainElements, ChainNodePath::trackLevel(trackId),
                                pads, [&visited](const RackInfo& rack, const ChainNodePath&) {
                                    visited.push_back(rack.name);
                                });
        return visited;
    };

    const auto entered = walk(chain_walk::Pads::Enter);
    REQUIRE(entered.size() == 2);
    CHECK(entered[0] == "Ordinary");
    CHECK(isPadRackId(tm.getPads(gridPath)->id));

    CHECK(walk(chain_walk::Pads::Skip).size() == 1);

    // Addressed by the owning device's path: a bare PadRack step is not a valid
    // address, and every pad API takes the grid's path and builds the rest.
    std::vector<ChainNodePath> rackPaths;
    chain_walk::forEachRack(
        track->chain.fxChainElements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Enter,
        [&rackPaths](const RackInfo&, const ChainNodePath& path) { rackPaths.push_back(path); });
    REQUIRE(rackPaths.size() == 2);
    CHECK(rackPaths[1] == gridPath);

    tm.clearAllTracks();
}

TEST_CASE("The rack walk is outermost first and can prune", "[chain-walk]") {
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto trackId = tm.createTrack("Track");
    const auto outerId = tm.addRackToTrack(trackId, "Outer");
    const auto outerPath = ChainNodePath::rack(trackId, outerId);
    const auto chainId = tm.addChainToRack(outerPath);
    const auto innerId = tm.addRackToChainByPath(outerPath.withChain(chainId), "Inner");
    REQUIRE(innerId != INVALID_RACK_ID);

    const auto* track = tm.getTrack(trackId);
    REQUIRE(track != nullptr);

    std::vector<juce::String> visited;
    chain_walk::forEachRack(
        track->chain.fxChainElements, ChainNodePath::trackLevel(trackId), chain_walk::Pads::Skip,
        [&visited](const RackInfo& rack, const ChainNodePath&) { visited.push_back(rack.name); });

    CHECK(order(visited) == "Outer,Inner");

    tm.clearAllTracks();
}
