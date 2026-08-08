#include <juce_core/juce_core.h>

#include "magda/daw/api/track_api_live.hpp"
#include "magda/daw/core/TrackManager.hpp"

// The facade's path-addressed rack and chain surface (#1993), driven against the
// real `TrackManager` rather than a mock.
//
// test_track_api_nested_racks.cpp covers the same operations through
// `MockTrackApi`, which is worth having — it is what stops the mock quietly
// agreeing with a facade that has lost depth again. But a mock I wrote passing
// tests I wrote proves the mock, not the forwarding. These run the same shapes
// through `TrackApiLive`, so what is asserted is that the real model answers.
//
// They live in the JUCE target because `TrackManager` is a singleton whose
// notifications assume an initialised message system.

namespace {

using namespace magda;

class TrackApiNestedRacksLiveTest final : public juce::UnitTest {
  public:
    TrackApiNestedRacksLiveTest() : juce::UnitTest("Track API Nested Racks Live", "magda") {}

    void runTest() override {
        beginTest("A chain and a device are reachable inside a nested rack");
        {
            Fixture fixture;
            TrackApiLive api;

            const auto track = api.createTrack("Bus", TrackType::Audio);
            const auto outerRack = api.addRackToTrack(track, "Outer");
            expect(outerRack != INVALID_RACK_ID);

            const auto outerRackPath = ChainNodePath::rack(track, outerRack);
            const auto* rack = api.getRackByPath(outerRackPath);
            expect(rack != nullptr);
            expect(!rack->chains.empty());
            const auto outerChainPath = outerRackPath.withChain(rack->chains.front().id);

            // Nest a rack inside that chain, then a chain inside *that* rack.
            // This is the shape the triple-based surface could not name.
            const auto innerRack = api.addRackToChainByPath(outerChainPath, "Inner");
            expect(innerRack != INVALID_RACK_ID);
            const auto innerRackPath = outerChainPath.withRack(innerRack);
            expect(api.getRackByPath(innerRackPath) != nullptr);

            const auto innerChain = api.addChainToRack(innerRackPath, "Deep");
            expect(innerChain != INVALID_CHAIN_ID);
            const auto innerChainPath = innerRackPath.withChain(innerChain);

            const auto* deep = api.getChainByPath(innerChainPath);
            expect(deep != nullptr);
            expectEquals(deep->name, juce::String("Deep"));

            // And a device inside it.
            DeviceInfo device;
            device.name = "Reverb";
            const auto deviceId = api.addDeviceToChainByPath(innerChainPath, device);
            expect(deviceId != INVALID_DEVICE_ID);

            const auto* withDevice = api.getChainByPath(innerChainPath);
            expect(withDevice != nullptr);
            expect(withDevice->elements.size() == 1);
            expect(magda::isDevice(withDevice->elements.front()));

            // The triple form cannot see the inner rack, because there is no
            // triple that names it — the limitation this issue is about.
            expect(api.getRack(track, innerRack) == nullptr);
        }

        beginTest("The two setters that had no path form reach a nested chain");
        {
            // `setChainOutput` and `setChainName` were triple-only on
            // TrackManager, so the facade could reach a nested chain's mute and
            // volume but not its name or output. Both are new path forms, so
            // unlike the rest of this surface they are not merely forwarded.
            Fixture fixture;
            TrackApiLive api;

            const auto track = api.createTrack("Bus", TrackType::Audio);
            const auto outerRack = api.addRackToTrack(track, "Outer");
            const auto outerRackPath = ChainNodePath::rack(track, outerRack);
            const auto outerChainPath =
                outerRackPath.withChain(api.getRackByPath(outerRackPath)->chains.front().id);
            const auto innerRackPath =
                outerChainPath.withRack(api.addRackToChainByPath(outerChainPath, "Inner"));
            const auto innerChainPath =
                innerRackPath.withChain(api.addChainToRack(innerRackPath, "Deep"));

            api.setChainName(innerChainPath, "Renamed");
            api.setChainOutput(innerChainPath, 2);
            api.setChainVolume(innerChainPath, -6.0f);

            const auto* chain = api.getChainByPath(innerChainPath);
            expect(chain != nullptr);
            expectEquals(chain->name, juce::String("Renamed"));
            expectEquals(chain->outputIndex, 2);
            expectWithinAbsoluteError(chain->volume, -6.0f, 0.001f);

            // The outer chain is untouched: a path resolving one level off would
            // be the quiet failure worth catching.
            const auto* outer = api.getChainByPath(outerChainPath);
            expect(outer != nullptr);
            expect(outer->name != "Renamed");
            expectEquals(outer->outputIndex, 0);
        }

        beginTest("The triple form and the path form address the same chain");
        {
            // The triples are kept as shims over the path surface, so this is
            // the equivalence the whole arrangement rests on.
            Fixture fixture;
            TrackApiLive api;

            const auto track = api.createTrack("Bus", TrackType::Audio);
            const auto rackId = api.addRackToTrack(track, "Outer");
            const auto rackPath = ChainNodePath::rack(track, rackId);
            const auto chainId = api.getRackByPath(rackPath)->chains.front().id;
            const auto chainPath = rackPath.withChain(chainId);

            api.setChainName(track, rackId, chainId, "Via triple");
            expectEquals(api.getChainByPath(chainPath)->name, juce::String("Via triple"));

            api.setChainName(chainPath, "Via path");
            expectEquals(api.getChain(track, rackId, chainId)->name, juce::String("Via path"));

            expect(api.getChain(track, rackId, chainId) == api.getChainByPath(chainPath));
            expect(api.getRack(track, rackId) == api.getRackByPath(rackPath));
        }

        beginTest("An unresolvable path changes nothing");
        {
            Fixture fixture;
            TrackApiLive api;

            const auto track = api.createTrack("Bus", TrackType::Audio);
            const auto rackId = api.addRackToTrack(track, "Outer");
            const auto rackPath = ChainNodePath::rack(track, rackId);
            const auto chainId = api.getRackByPath(rackPath)->chains.front().id;
            const auto chainPath = rackPath.withChain(chainId);

            expect(api.getRackByPath(ChainNodePath::rack(track, 9999)) == nullptr);
            expect(api.getChainByPath(ChainNodePath::chain(track, rackId, 9999)) == nullptr);
            // Asking for a chain with a rack path is a miss.
            expect(api.getChainByPath(rackPath) == nullptr);

            // The other direction is not. `getRackByPath` returns the deepest
            // rack it walked through regardless of the final step, so a chain
            // path answers with that chain's parent rack. Long-standing
            // `TrackManager` behaviour, pinned here because it is surprising —
            // this test is what caught the mock disagreeing about it, and
            // tightening it would reach well beyond this facade. Tracked as #2057.
            expect(api.getRackByPath(chainPath) == api.getRackByPath(rackPath));

            // A path whose *middle* step is broken must not resolve to
            // something it merely passed through. This is the case the mock
            // could not catch: it failed closed at the first miss while the
            // real resolver carried the last rack it had seen forward, so a
            // write through `outer > missingChain > missingRack` landed on
            // `outer` — a mutation of a node the caller never named.
            const auto brokenMiddle = rackPath.withChain(9999).withRack(9998);
            expect(api.getRackByPath(brokenMiddle) == nullptr);

            // And the nastier shape: the trailing step names a rack that really
            // does exist, just somewhere else entirely. The missed chain step
            // used to leave the traversal at track level, so this resolved to
            // that unrelated top-level rack.
            const auto secondTopLevel = api.addRackToTrack(track, "Elsewhere");
            const auto brokenToElsewhere = rackPath.withChain(9999).withRack(secondTopLevel);
            expect(api.getRackByPath(brokenToElsewhere) == nullptr);

            // The writes that resolve through it are therefore no-ops rather
            // than misdirected. `setRackVolume` is the clearest: it is new
            // facade surface in this change, so the blast radius was new too.
            const auto* outerBefore = api.getRackByPath(rackPath);
            expect(outerBefore != nullptr);
            const auto volumeBefore = outerBefore->volume;
            const auto bypassedBefore = outerBefore->bypassed;

            api.setRackVolume(brokenMiddle, -24.0f);
            api.setRackBypassedByPath(brokenMiddle, true);
            const auto chainsBefore = api.getRackByPath(rackPath)->chains.size();
            api.addChainToRack(brokenMiddle, "Should not appear");

            const auto* outerAfter = api.getRackByPath(rackPath);
            expect(outerAfter != nullptr);
            expectWithinAbsoluteError(outerAfter->volume, volumeBefore, 0.001f);
            expect(outerAfter->bypassed == bypassedBefore);
            expect(outerAfter->chains.size() == chainsBefore);

            api.setChainName(ChainNodePath::chain(track, rackId, 9999), "Nowhere");
            expect(api.getChainByPath(chainPath)->name != "Nowhere");
        }

        beginTest("A structurally impossible route is refused, even with real ids");
        {
            // The harder case than a missing id: every step below names
            // something that genuinely exists, and only the *shape* is wrong.
            // Rejecting a miss is not enough, because two consecutive steps of
            // the same kind walk sideways through the tree on ids that resolve.
            Fixture fixture;
            TrackApiLive api;

            const auto track = api.createTrack("Bus", TrackType::Audio);
            const auto rackA = api.addRackToTrack(track, "A");
            const auto rackB = api.addRackToTrack(track, "B");
            const auto pathA = ChainNodePath::rack(track, rackA);
            const auto pathB = ChainNodePath::rack(track, rackB);

            // Rack after Rack. `rackB` is a *sibling* of `rackA`, not inside
            // it — with the chain context cleared, the second step used to
            // search the track list again and find it.
            expect(api.getRackByPath(pathA.withRack(rackB)) == nullptr);

            // Chain after Chain. Two chains in one rack; the second is a
            // sibling of the first, reached by a route that passes through
            // nothing.
            const auto chain1 = api.getRackByPath(pathA)->chains.front().id;
            const auto chain2 = api.addChainToRack(pathA, "Second");
            expect(chain2 != INVALID_CHAIN_ID);
            expect(api.getChainByPath(pathA.withChain(chain1).withChain(chain2)) == nullptr);
            expect(api.getRackByPath(pathA.withChain(chain1).withChain(chain2)) == nullptr);

            // A device is a leaf; nothing follows it.
            DeviceInfo device;
            device.name = "Leaf";
            const auto deviceId = api.addDeviceToChainByPath(pathA.withChain(chain1), device);
            expect(deviceId != INVALID_DEVICE_ID);
            const auto validDevicePath = pathA.withChain(chain1).withDevice(deviceId);
            expect(api.getRackByPath(validDevicePath) == api.getRackByPath(pathA));
            const auto missingDevicePath = pathA.withChain(chain1).withDevice(9999);
            expect(api.getRackByPath(missingDevicePath) == nullptr);
            expect(api.getRackByPath(pathA.withDevice(deviceId)) == nullptr);
            expect(api.getRackByPath(
                       pathA.withChain(chain1).withDevice(deviceId).withRack(rackB)) == nullptr);

            // An explicit Segment selects a flat section, so it cannot lead to
            // a rack in the main FX list even when that rack id exists.
            auto segmentedRack = pathA;
            segmentedRack.steps.insert(
                segmentedRack.steps.begin(),
                {ChainStepType::Segment, static_cast<int>(ChainSegment::PostFx)});
            expect(api.getRackByPath(segmentedRack) == nullptr);

            // And the writes that resolve through it leave both racks alone,
            // rather than landing on the sibling the route wandered into.
            const auto volumeB = api.getRackByPath(pathB)->volume;
            const auto chainsB = api.getRackByPath(pathB)->chains.size();
            const auto volumeA = api.getRackByPath(pathA)->volume;
            api.setRackVolume(pathA.withRack(rackB), -24.0f);
            api.setRackBypassedByPath(pathA.withRack(rackB), true);
            api.addChainToRack(pathA.withRack(rackB), "Should not appear");
            api.setRackVolume(missingDevicePath, -24.0f);
            api.setRackVolume(segmentedRack, -24.0f);

            const auto* afterB = api.getRackByPath(pathB);
            expect(afterB != nullptr);
            expectWithinAbsoluteError(afterB->volume, volumeB, 0.001f);
            expect(!afterB->bypassed);
            expect(afterB->chains.size() == chainsB);
            expectWithinAbsoluteError(api.getRackByPath(pathA)->volume, volumeA, 0.001f);
        }

        beginTest("A device is not reachable through a sideways route either");
        {
            // Device paths are the ones a remote client supplies verbatim —
            // `toChainNodePath` builds them from whatever steps arrive — so a
            // malformed route here reaches a real device in a part of the tree
            // the caller never named.
            //
            // Worth its own case because the device resolver went through a
            // *second* copy of the chain lookup, which did not learn the
            // structural guards when `getChainByPath` did. Both now share one
            // body.
            Fixture fixture;
            TrackManager& model = TrackManager::getInstance();
            TrackApiLive api;

            const auto track = api.createTrack("Bus", TrackType::Audio);
            const auto rackId = api.addRackToTrack(track, "Outer");
            const auto rackPath = ChainNodePath::rack(track, rackId);
            const auto chain1 = api.getRackByPath(rackPath)->chains.front().id;
            const auto chain2 = api.addChainToRack(rackPath, "Second");

            // A real device, in the second chain.
            DeviceInfo device;
            device.name = "Hidden";
            const auto deviceId = api.addDeviceToChainByPath(rackPath.withChain(chain2), device);
            expect(deviceId != INVALID_DEVICE_ID);

            // Reachable by its own route.
            const auto realRoute = rackPath.withChain(chain2).withDevice(deviceId);
            expect(model.getDeviceInChainByPath(realRoute) != nullptr);

            // Not by one that hops from its sibling chain into it.
            const auto sideways = rackPath.withChain(chain1).withChain(chain2).withDevice(deviceId);
            expect(model.getDeviceInChainByPath(sideways) == nullptr);

            // Nor by one that claims it sits directly under a doubled rack step.
            const auto doubledRack =
                rackPath.withRack(rackId).withChain(chain2).withDevice(deviceId);
            expect(model.getDeviceInChainByPath(doubledRack) == nullptr);
        }
    }

  private:
    /// TrackManager is a process-wide singleton, so a test that adds tracks has
    /// to clear them or it leaks into whatever runs next.
    struct Fixture {
        Fixture() {
            TrackManager::getInstance().clearAllTracks();
        }
        ~Fixture() {
            TrackManager::getInstance().clearAllTracks();
        }
    };
};

TrackApiNestedRacksLiveTest trackApiNestedRacksLiveTest;

}  // namespace
