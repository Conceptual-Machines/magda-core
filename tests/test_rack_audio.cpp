#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/RackInfo.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture Helper
// ============================================================================

class RackAudioTestFixture {
  public:
    RackAudioTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~RackAudioTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }
};

// ============================================================================
// Rack Data Model Integration Tests
// ============================================================================

TEST_CASE("Rack audio sync: data model preparation", "[rack_audio][data_model]") {
    RackAudioTestFixture fixture;

    SECTION("Rack with devices has correct structure for sync") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "FX Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack != nullptr);
        REQUIRE(rack->chains.size() == 1);

        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        // Add devices to the chain
        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";

        DeviceInfo reverb;
        reverb.name = "Reverb";
        reverb.format = PluginFormat::Internal;
        reverb.pluginId = "reverb";

        auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);
        auto reverbId = fixture.tm().addDeviceToChainByPath(chainPath, reverb);

        REQUIRE(delayId != INVALID_DEVICE_ID);
        REQUIRE(reverbId != INVALID_DEVICE_ID);

        // Verify the rack is in the track's chain elements
        auto* track = fixture.tm().getTrack(trackId);
        REQUIRE(track != nullptr);
        REQUIRE(track->chainElements.size() == 1);
        REQUIRE(isRack(track->chainElements[0]));

        const auto& rackElement = getRack(track->chainElements[0]);
        REQUIRE(rackElement.id == rackId);
        REQUIRE(rackElement.chains[0].elements.size() == 2);
    }

    SECTION("Rack with multiple chains for parallel processing") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Parallel Rack");

        auto rackPath = ChainNodePath::rack(trackId, rackId);

        // Add a second chain
        auto chain2Id = fixture.tm().addChainToRack(rackPath, "Chain 2");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains.size() == 2);

        // Add different devices to each chain
        auto chain1Path = rackPath.withChain(rack->chains[0].id);
        auto chain2Path = rackPath.withChain(chain2Id);

        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";

        DeviceInfo reverb;
        reverb.name = "Reverb";
        reverb.format = PluginFormat::Internal;
        reverb.pluginId = "reverb";

        fixture.tm().addDeviceToChainByPath(chain1Path, delay);
        fixture.tm().addDeviceToChainByPath(chain2Path, reverb);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 1);
        REQUIRE(rack->chains[1].elements.size() == 1);
    }

    SECTION("Rack chain mute/solo state") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE_FALSE(rack->chains[0].muted);
        REQUIRE_FALSE(rack->chains[0].solo);

        // Modify chain mute state
        rack->chains[0].muted = true;
        REQUIRE(rack->chains[0].muted);

        // Modify chain solo state
        rack->chains[0].solo = true;
        REQUIRE(rack->chains[0].solo);
    }

    SECTION("Rack bypass state") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE_FALSE(rack->bypassed);

        fixture.tm().setRackBypassed(trackId, rackId, true);
        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->bypassed);
    }

    SECTION("Rack chain volume and pan") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].volume == 0.0f);  // 0 dB (unity)
        REQUIRE(rack->chains[0].pan == 0.0f);     // Center

        // Set chain volume and pan
        rack->chains[0].volume = -6.0f;
        rack->chains[0].pan = 0.5f;

        REQUIRE(rack->chains[0].volume == -6.0f);
        REQUIRE(rack->chains[0].pan == 0.5f);
    }
}

TEST_CASE("Rack audio sync: macro and mod structure", "[rack_audio][macros][mods]") {
    RackAudioTestFixture fixture;

    SECTION("Rack has default macros") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->macros.size() == NUM_MACROS);
    }

    SECTION("Rack macro can link to device parameter") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";
        auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);

        // Link macro 0 to the delay's parameter 0
        rack = fixture.tm().getRack(trackId, rackId);
        MacroLink link;
        link.target.deviceId = delayId;
        link.target.paramIndex = 0;
        link.amount = 0.75f;
        rack->macros[0].links.push_back(link);

        REQUIRE(rack->macros[0].isLinked());
        REQUIRE(rack->macros[0].links.size() == 1);
        REQUIRE(rack->macros[0].links[0].target.deviceId == delayId);
    }

    SECTION("Rack mod can link to device parameter") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo eq;
        eq.name = "EQ";
        eq.format = PluginFormat::Internal;
        eq.pluginId = "eq";
        auto eqId = fixture.tm().addDeviceToChainByPath(chainPath, eq);

        // Add a default mod page so we have mods to work with
        rack = fixture.tm().getRack(trackId, rackId);
        addModPage(rack->mods);
        REQUIRE(rack->mods.size() > 0);

        ModLink link;
        link.target.deviceId = eqId;
        link.target.paramIndex = 0;
        link.amount = 0.5f;
        rack->mods[0].addLink(link.target, link.amount);

        REQUIRE(rack->mods[0].isLinked());
        REQUIRE(rack->mods[0].links.size() == 1);
    }
}

TEST_CASE("Rack audio sync: recursive device search", "[rack_audio][recursive_search]") {
    RackAudioTestFixture fixture;

    SECTION("Device inside rack is findable") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo delay;
        delay.name = "Delay";
        delay.format = PluginFormat::Internal;
        delay.pluginId = "delay";
        auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);

        // The device should be findable via the path resolution
        auto devicePath = chainPath.withDevice(delayId);
        auto* foundDevice = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(foundDevice != nullptr);
        REQUIRE(foundDevice->name == "Delay");
    }

    SECTION("Top-level device coexists with rack") {
        auto trackId = fixture.tm().createTrack("Test Track");

        // Add a top-level device first
        DeviceInfo topDevice;
        topDevice.name = "Top EQ";
        topDevice.format = PluginFormat::Internal;
        topDevice.pluginId = "eq";
        fixture.tm().addDeviceToTrack(trackId, topDevice);

        // Add a rack
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* track = fixture.tm().getTrack(trackId);
        REQUIRE(track->chainElements.size() == 2);
        REQUIRE(isDevice(track->chainElements[0]));
        REQUIRE(isRack(track->chainElements[1]));
    }
}
