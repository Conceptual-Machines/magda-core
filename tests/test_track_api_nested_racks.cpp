// Racks and chains at depth, through the facade (#1993).
//
// `TrackApi` addressed racks and chains by a `(trackId, rackId, chainId)`
// triple, which names exactly one level of nesting. The model nests
// arbitrarily — `Track > Rack > Chain > Rack > Chain > Device` — so nothing
// inside a nested rack was reachable through the facade at all: there is no
// triple that names the inner chain. A caller could already address a *device*
// at depth, because `DevicePathDto` has carried a full route since #1991, but
// not the chain containing it, so "add a chain to this rack" stopped working at
// depth two.
//
// These drive `MockTrackApi`, which models the same nested tree `TrackManager`
// holds and resolves paths the same way. That is deliberate: the mock used to
// stub every rack and chain method to `INVALID_*_ID`, which agreed with a
// facade that could not reach depth, so the gap was invisible from here.

#include <catch2/catch_test_macros.hpp>

#include "MockMagdaApi.hpp"

using namespace magda;
using magda::test::MockMagdaApi;

namespace {

/// `Track > Rack > Chain`, the shallowest thing worth nesting into.
struct TopLevel {
    TrackId track = INVALID_TRACK_ID;
    RackId rack = INVALID_RACK_ID;
    ChainId chain = INVALID_CHAIN_ID;

    ChainNodePath rackPath() const {
        return ChainNodePath::rack(track, rack);
    }
    ChainNodePath chainPath() const {
        return ChainNodePath::chain(track, rack, chain);
    }
};

TopLevel makeTopLevel(MockMagdaApi& api) {
    TopLevel built;
    built.track = api.tracks().createTrack("Bus", TrackType::Audio);
    built.rack = api.tracks().addRackToTrack(built.track, "Outer");
    REQUIRE(built.rack != INVALID_RACK_ID);

    // A new rack comes with one chain, so this is the chain to nest into.
    const auto* rack = api.tracks().getRackByPath(built.rackPath());
    REQUIRE(rack != nullptr);
    REQUIRE_FALSE(rack->chains.empty());
    built.chain = rack->chains.front().id;
    return built;
}

}  // namespace

TEST_CASE("A rack can be created inside a chain", "[track-api][racks][nesting]") {
    MockMagdaApi api;
    const auto top = makeTopLevel(api);

    const auto innerRack = api.tracks().addRackToChainByPath(top.chainPath(), "Inner");
    REQUIRE(innerRack != INVALID_RACK_ID);
    REQUIRE(innerRack != top.rack);

    // Reachable by the route that names it, and by nothing shorter.
    const auto innerPath = top.chainPath().withRack(innerRack);
    const auto* found = api.tracks().getRackByPath(innerPath);
    REQUIRE(found != nullptr);
    REQUIRE(found->name == "Inner");

    // The triple-based surface cannot see it: `(track, innerRack)` asks for a
    // top-level rack with that id, and there is none. This is the limitation
    // the path form exists to lift, asserted rather than described.
    REQUIRE(api.tracks().getRack(top.track, innerRack) == nullptr);
}

TEST_CASE("A chain can be created inside a nested rack", "[track-api][racks][nesting]") {
    MockMagdaApi api;
    const auto top = makeTopLevel(api);
    const auto innerRack = api.tracks().addRackToChainByPath(top.chainPath(), "Inner");
    const auto innerRackPath = top.chainPath().withRack(innerRack);

    // The operation the issue names: at depth two this had no expressible form.
    const auto innerChain = api.tracks().addChainToRack(innerRackPath, "Deep");
    REQUIRE(innerChain != INVALID_CHAIN_ID);

    const auto innerChainPath = innerRackPath.withChain(innerChain);
    const auto* chain = api.tracks().getChainByPath(innerChainPath);
    REQUIRE(chain != nullptr);
    REQUIRE(chain->name == "Deep");

    // Depth is four steps: Rack > Chain > Rack > Chain.
    REQUIRE(innerChainPath.depth() == 4);
}

TEST_CASE("A device can be added to a chain inside a nested rack", "[track-api][racks][nesting]") {
    MockMagdaApi api;
    const auto top = makeTopLevel(api);
    const auto innerRack = api.tracks().addRackToChainByPath(top.chainPath(), "Inner");
    const auto innerRackPath = top.chainPath().withRack(innerRack);
    const auto innerChain = api.tracks().addChainToRack(innerRackPath, "Deep");
    const auto innerChainPath = innerRackPath.withChain(innerChain);

    DeviceInfo device;
    device.name = "Reverb";
    const auto deviceId = api.tracks().addDeviceToChainByPath(innerChainPath, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const auto* chain = api.tracks().getChainByPath(innerChainPath);
    REQUIRE(chain != nullptr);
    REQUIRE(chain->elements.size() == 1);
    REQUIRE(magda::isDevice(chain->elements.front()));
    REQUIRE(magda::getDevice(chain->elements.front()).name == "Reverb");
}

TEST_CASE("Every chain setter reaches a nested chain", "[track-api][racks][nesting]") {
    // The whole setter surface, not a sample: the gap was that *some* of these
    // had a path form and some did not, and a caller has no way to know which
    // until one silently does nothing. `setChainOutput` and `setChainName` are
    // the two that had none before this change.
    MockMagdaApi api;
    const auto top = makeTopLevel(api);
    const auto innerRack = api.tracks().addRackToChainByPath(top.chainPath(), "Inner");
    const auto innerChainPath = top.chainPath().withRack(innerRack).withChain(
        api.tracks().addChainToRack(top.chainPath().withRack(innerRack), "Deep"));

    auto& tracks = api.tracks();
    tracks.setChainName(innerChainPath, "Renamed");
    tracks.setChainOutput(innerChainPath, 3);
    tracks.setChainMuted(innerChainPath, true);
    tracks.setChainBypassed(innerChainPath, true);
    tracks.setChainSolo(innerChainPath, true);
    tracks.setChainVolume(innerChainPath, -6.0f);
    tracks.setChainPan(innerChainPath, -0.5f);

    const auto* chain = tracks.getChainByPath(innerChainPath);
    REQUIRE(chain != nullptr);
    REQUIRE(chain->name == "Renamed");
    REQUIRE(chain->outputIndex == 3);
    REQUIRE(chain->muted);
    REQUIRE(chain->bypassed);
    REQUIRE(chain->solo);
    REQUIRE(chain->volume == -6.0f);
    REQUIRE(chain->pan == -0.5f);
}

TEST_CASE("Rack setters reach a nested rack", "[track-api][racks][nesting]") {
    MockMagdaApi api;
    const auto top = makeTopLevel(api);
    const auto innerRack = api.tracks().addRackToChainByPath(top.chainPath(), "Inner");
    const auto innerRackPath = top.chainPath().withRack(innerRack);

    api.tracks().setRackBypassedByPath(innerRackPath, true);
    api.tracks().setRackVolume(innerRackPath, -3.0f);

    const auto* rack = api.tracks().getRackByPath(innerRackPath);
    REQUIRE(rack != nullptr);
    REQUIRE(rack->bypassed);
    REQUIRE(rack->volume == -3.0f);

    // And the outer rack is untouched — a path that resolved to the wrong level
    // would be the failure mode worth catching.
    const auto* outer = api.tracks().getRackByPath(top.rackPath());
    REQUIRE(outer != nullptr);
    REQUIRE_FALSE(outer->bypassed);
}

TEST_CASE("A nested chain and rack can be removed", "[track-api][racks][nesting]") {
    MockMagdaApi api;
    const auto top = makeTopLevel(api);
    const auto innerRack = api.tracks().addRackToChainByPath(top.chainPath(), "Inner");
    const auto innerRackPath = top.chainPath().withRack(innerRack);
    const auto innerChain = api.tracks().addChainToRack(innerRackPath, "Deep");
    const auto innerChainPath = innerRackPath.withChain(innerChain);

    REQUIRE(api.tracks().getChainByPath(innerChainPath) != nullptr);
    api.tracks().removeChainByPath(innerChainPath);
    REQUIRE(api.tracks().getChainByPath(innerChainPath) == nullptr);
    // Its rack survives the loss of one of its chains.
    REQUIRE(api.tracks().getRackByPath(innerRackPath) != nullptr);

    api.tracks().removeRackFromChainByPath(innerRackPath);
    REQUIRE(api.tracks().getRackByPath(innerRackPath) == nullptr);
    // The rack it was nested in is still there.
    REQUIRE(api.tracks().getRackByPath(top.rackPath()) != nullptr);
}

TEST_CASE("The triple-based surface is the path form at depth one", "[track-api][racks][nesting]") {
    // The triples are kept as shims rather than removed, because the agent DSL
    // addresses one rack at a time by design. This pins the equivalence they
    // rest on: `(t, r, c)` is `ChainNodePath::chain(t, r, c)` and nothing else.
    MockMagdaApi api;
    const auto top = makeTopLevel(api);

    api.tracks().setChainName(top.track, top.rack, top.chain, "Via triple");
    REQUIRE(api.tracks().getChainByPath(top.chainPath())->name == "Via triple");

    api.tracks().setChainName(top.chainPath(), "Via path");
    REQUIRE(api.tracks().getChain(top.track, top.rack, top.chain)->name == "Via path");

    // Both lookups answer with the same object.
    REQUIRE(api.tracks().getChain(top.track, top.rack, top.chain) ==
            api.tracks().getChainByPath(top.chainPath()));
    REQUIRE(api.tracks().getRack(top.track, top.rack) ==
            api.tracks().getRackByPath(top.rackPath()));
}

TEST_CASE("An unresolvable path yields nothing rather than the wrong node",
          "[track-api][racks][nesting]") {
    MockMagdaApi api;
    const auto top = makeTopLevel(api);

    // A rack id that exists at another level, a chain id that does not exist,
    // and a path whose last step is the wrong kind. Each must be a miss: the
    // dangerous failure for a path resolver is answering with a near neighbour.
    REQUIRE(api.tracks().getRackByPath(ChainNodePath::rack(top.track, 9999)) == nullptr);
    REQUIRE(api.tracks().getChainByPath(ChainNodePath::chain(top.track, top.rack, 9999)) ==
            nullptr);
    REQUIRE(api.tracks().getChainByPath(top.rackPath()) == nullptr);
    REQUIRE(api.tracks().getRackByPath(ChainNodePath::rack(9999, top.rack)) == nullptr);

    // Asking for a *rack* with a chain path is lenient rather than a miss: the
    // resolver answers with the deepest rack it walked through, which is that
    // chain's parent. Pinned because it is surprising, and because the live
    // test caught the mock disagreeing with `TrackManager` about it — not
    // because it is obviously the right rule. Tightening it would change
    // long-standing behaviour well outside this facade — tracked as #2057.
    REQUIRE(api.tracks().getRackByPath(top.chainPath()) ==
            api.tracks().getRackByPath(top.rackPath()));

    // A path whose *middle* step is broken resolves to nothing, rather than to
    // something it merely walked through on the way. The live suite is what
    // pins this against the real resolver — this mock always failed closed, and
    // `TrackManager` did not, so the agreement here is the thing that used to be
    // a lie. See `test_track_api_nested_racks_live_juce.cpp`.
    REQUIRE(api.tracks().getRackByPath(top.rackPath().withChain(9999).withRack(9998)) == nullptr);

    // Writing through an unresolvable path changes nothing rather than
    // asserting or landing somewhere else.
    api.tracks().setChainName(ChainNodePath::chain(top.track, top.rack, 9999), "Nowhere");
    REQUIRE(api.tracks().getChainByPath(top.chainPath())->name != "Nowhere");

    const auto* outerBefore = api.tracks().getRackByPath(top.rackPath());
    REQUIRE(outerBefore != nullptr);
    const auto chainsBefore = outerBefore->chains.size();
    api.tracks().setRackVolume(top.rackPath().withChain(9999).withRack(9998), -24.0f);
    api.tracks().addChainToRack(top.rackPath().withChain(9999).withRack(9998), "Nowhere");
    REQUIRE(api.tracks().getRackByPath(top.rackPath())->volume != -24.0f);
    REQUIRE(api.tracks().getRackByPath(top.rackPath())->chains.size() == chainsBefore);
}
