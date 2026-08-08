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
            // tightening it would reach well beyond this facade.
            expect(api.getRackByPath(chainPath) == api.getRackByPath(rackPath));

            api.setChainName(ChainNodePath::chain(track, rackId, 9999), "Nowhere");
            expect(api.getChainByPath(chainPath)->name != "Nowhere");
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
