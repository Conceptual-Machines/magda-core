#include <catch2/catch_test_macros.hpp>
#include <string>

#include "magda/daw/core/ChainWalk.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/MacroInfo.hpp"
#include "magda/daw/core/ModInfo.hpp"
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

TEST_CASE("A device's address survives a round trip through its parent", "[chain-walk]") {
    // deviceIn() has to agree with parentChain() about how the track's own list
    // is spelled, or a caller that has a device path, takes its parent and asks
    // for the device back gets a different path naming the same device.
    // parentChain() leaves isTrackLevel unset, so keying on that flag broke
    // exactly this (#2204).
    const auto topLevel = ChainNodePath::topLevelDevice(7, 3);
    CHECK(chain_walk::deviceIn(topLevel.parentChain(), 3) == topLevel);

    const auto nested = ChainNodePath::rack(7, 1).withChain(2).withDevice(3);
    CHECK(chain_walk::deviceIn(nested.parentChain(), 3) == nested);

    const auto onPad = ChainNodePath::padChain(7, 3, 4).withDevice(5);
    CHECK(chain_walk::deviceIn(onPad.parentChain(), 5) == onPad);
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

// ============================================================================
// What the descent covering pads is worth, at the two places it did not.
//
// Each becomes wrong the moment a pad device owns the thing being collected,
// and nothing says so -- the trap #2204 describes. Both were found by putting
// the walks named in that issue onto one descent and asking each what it does
// about pads.
// ============================================================================

TEST_CASE("Moving a Drum Grid off a track drops that track's links into its pads",
          "[chain-walk][links]") {
    // A move looks every device it carried up in a map the descent built, and
    // for a cross-track move drops the source track's links to them: a track
    // macro cannot reach into another track. That descent stopped at the grid,
    // so a link into one of its pads was never in the map and survived,
    // pointing at a device the track no longer holds.
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto source = tm.createTrack("Source");
    const auto destination = tm.createTrack("Destination");

    const auto gridId = tm.addDeviceToTrack(source, drumGrid("Grid"));
    const auto gridPath = ChainNodePath::topLevelDevice(source, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    REQUIRE(padChainId != INVALID_CHAIN_ID);
    const auto padDeviceId = tm.addDeviceToPad(gridPath, padChainId, effect("OnPad"));
    REQUIRE(padDeviceId != INVALID_DEVICE_ID);

    const auto padDevicePath =
        ChainNodePath::padChain(source, gridId, padChainId).withDevice(padDeviceId);

    // A track macro driving a parameter of the device on the pad.
    auto* sourceTrack = tm.getTrack(source);
    REQUIRE(sourceTrack != nullptr);
    sourceTrack->macros[0].links.push_back(
        MacroLink{ControlTarget::pluginParam(padDevicePath, 0), 1.0f, true});

    REQUIRE(tm.getTrack(source)->macros[0].links.size() == 1);
    REQUIRE(tm.moveChainElement(gridPath, ChainNodePath::trackLevel(destination), 0));

    CHECK(tm.getTrack(source)->macros[0].links.empty());

    // And the grid really did move, so the link had nothing left to name.
    CHECK(tm.getDeviceInChainByPath(
              ChainNodePath::padChain(destination, gridId, padChainId).withDevice(padDeviceId)) !=
          nullptr);

    tm.clearAllTracks();
}

TEST_CASE("Duplicating a track addresses the copy's pads the typed way", "[chain-walk][links]") {
    // The duplicate remaps every link it copies onto the new track's ids, and
    // built a pad address as `Rack(deviceId) > Chain(padId)` -- the untyped
    // spelling #2219 removed. A rack step carrying a DeviceId resolves down the
    // rack walk, which no pad answers to, so the link named nothing.
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto source = tm.createTrack("Source");
    const auto gridId = tm.addDeviceToTrack(source, drumGrid("Grid"));
    const auto gridPath = ChainNodePath::topLevelDevice(source, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    const auto padDeviceId = tm.addDeviceToPad(gridPath, padChainId, effect("OnPad"));
    REQUIRE(padDeviceId != INVALID_DEVICE_ID);

    auto* padDevice = tm.getDeviceInChainByPath(
        ChainNodePath::padChain(source, gridId, padChainId).withDevice(padDeviceId));
    REQUIRE(padDevice != nullptr);
    // A mod link with no path of its own: the remap stamps the owner's address
    // into it, which is the one place the pad address the duplicate builds is
    // actually consumed. A link that already carries a path is remapped by id
    // instead and never sees it.
    // A device starts with no modifiers (`createDefaultMods(0)`), so one is
    // added before it can carry a link.
    padDevice->mods.push_back(ModInfo(0));
    padDevice->mods.back().links.push_back(
        ModLink{ControlTarget::modParam(ChainNodePath{}, 0, 0), 1.0f, true});

    const auto copy = tm.duplicateTrack(source);
    REQUIRE(copy != INVALID_TRACK_ID);

    // Whatever ids the copy got, the link its pad device holds has to name
    // something the model can resolve.
    const auto* copyTrack = tm.getTrack(copy);
    REQUIRE(copyTrack != nullptr);
    REQUIRE(copyTrack->chain.fxChainElements.size() == 1);
    const auto& copiedGrid = getDevice(copyTrack->chain.fxChainElements.front());
    REQUIRE(copiedGrid.pads);
    REQUIRE_FALSE(copiedGrid.pads->chains.empty());

    const auto& copiedPad = copiedGrid.pads->chains.front();
    REQUIRE_FALSE(copiedPad.elements.empty());
    const auto& copiedPadDevice = getDevice(copiedPad.elements.front());
    REQUIRE_FALSE(copiedPadDevice.mods.empty());
    REQUIRE_FALSE(copiedPadDevice.mods.front().links.empty());

    const auto& target = copiedPadDevice.mods.front().links.front().target.devicePath;
    CHECK(target.isPadOwned());
    CHECK(tm.getDeviceInChainByPath(target) != nullptr);

    tm.clearAllTracks();
}

TEST_CASE("A moved grid's pad device keeps its own links pointing at itself",
          "[chain-walk][links]") {
    // The links a moved device OWNS are retargeted onto its new address, by a
    // second descent over what arrived. That one skipped pads too, so a
    // modifier on a pad device pointing at its own parameter went on naming the
    // track the grid came from (#2204).
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto source = tm.createTrack("Source");
    const auto destination = tm.createTrack("Destination");

    const auto gridId = tm.addDeviceToTrack(source, drumGrid("Grid"));
    const auto gridPath = ChainNodePath::topLevelDevice(source, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    const auto padDeviceId = tm.addDeviceToPad(gridPath, padChainId, effect("OnPad"));
    REQUIRE(padDeviceId != INVALID_DEVICE_ID);

    const auto before = ChainNodePath::padChain(source, gridId, padChainId).withDevice(padDeviceId);
    auto* padDevice = tm.getDeviceInChainByPath(before);
    REQUIRE(padDevice != nullptr);
    padDevice->mods.push_back(ModInfo(0));
    padDevice->mods.back().links.push_back(
        ModLink{ControlTarget::pluginParam(before, 0), 1.0f, true});

    REQUIRE(tm.moveChainElement(gridPath, ChainNodePath::trackLevel(destination), 0));

    const auto after =
        ChainNodePath::padChain(destination, gridId, padChainId).withDevice(padDeviceId);
    const auto* moved = tm.getDeviceInChainByPath(after);
    REQUIRE(moved != nullptr);
    REQUIRE_FALSE(moved->mods.empty());
    REQUIRE_FALSE(moved->mods.front().links.empty());

    CHECK(moved->mods.front().links.front().target.devicePath == after);

    tm.clearAllTracks();
}

TEST_CASE("A pad device left behind loses its link to a device that moved away",
          "[chain-walk][links]") {
    // The mirror: the source track drops links naming a device that left, by a
    // descent that also skipped pads. A pad device staying put kept a link to
    // something no longer on its track.
    auto& tm = TrackManager::getInstance();
    tm.clearAllTracks();

    const auto source = tm.createTrack("Source");
    const auto destination = tm.createTrack("Destination");

    const auto gridId = tm.addDeviceToTrack(source, drumGrid("Stays"));
    const auto gridPath = ChainNodePath::topLevelDevice(source, gridId);
    const auto padChainId = tm.ensurePad(gridPath, 0);
    const auto padDeviceId = tm.addDeviceToPad(gridPath, padChainId, effect("OnPad"));
    REQUIRE(padDeviceId != INVALID_DEVICE_ID);

    const auto leavingId = tm.addDeviceToTrack(source, effect("Leaves"));
    const auto leavingPath = ChainNodePath::topLevelDevice(source, leavingId);

    const auto padDevicePath =
        ChainNodePath::padChain(source, gridId, padChainId).withDevice(padDeviceId);
    auto* padDevice = tm.getDeviceInChainByPath(padDevicePath);
    REQUIRE(padDevice != nullptr);
    padDevice->mods.push_back(ModInfo(0));
    padDevice->mods.back().links.push_back(
        ModLink{ControlTarget::pluginParam(leavingPath, 0), 1.0f, true});

    REQUIRE(tm.moveChainElement(leavingPath, ChainNodePath::trackLevel(destination), 0));

    const auto* stayed = tm.getDeviceInChainByPath(padDevicePath);
    REQUIRE(stayed != nullptr);
    REQUIRE_FALSE(stayed->mods.empty());
    CHECK(stayed->mods.front().links.empty());

    tm.clearAllTracks();
}
