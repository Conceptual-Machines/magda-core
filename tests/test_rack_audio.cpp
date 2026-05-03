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

TEST_CASE("MAGDA device presets retarget device-local macro and mod links",
          "[rack_audio][device_presets][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Preset Target");

    DeviceInfo liveTemplate;
    liveTemplate.name = "4OSC Synth";
    liveTemplate.format = PluginFormat::Internal;
    liveTemplate.pluginId = "4osc";

    auto liveDeviceId = fixture.tm().addDeviceToTrack(trackId, liveTemplate);
    auto livePath = ChainNodePath::topLevelDevice(trackId, liveDeviceId);

    DeviceInfo preset = liveTemplate;
    preset.id = 99;
    preset.parameters.resize(64);
    preset.pluginState = "<PLUGIN><MODIFIERASSIGNMENTS><LFO source=\"1\" paramID=\"filterFreq\" "
                         "value=\"0.5\"/></MODIFIERASSIGNMENTS></PLUGIN>";

    MacroInfo macro(0);
    macro.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::topLevelDevice(123, preset.id), 7), 0.25f,
         false});
    preset.macros = {macro};

    ModInfo mod(0);
    mod.links.push_back(
        {ControlTarget::pluginParam(ChainNodePath::topLevelDevice(123, preset.id), 8), 0.5f, true});
    preset.mods = {mod};

    REQUIRE(fixture.tm().applyDevicePreset(livePath, preset));

    auto* live = fixture.tm().getDeviceInChainByPath(livePath);
    REQUIRE(live != nullptr);
    REQUIRE(live->macros.size() == 1);
    REQUIRE(live->mods.size() == 1);
    REQUIRE(live->macros[0].links.size() == 1);
    REQUIRE(live->mods[0].links.size() == 1);

    CHECK(live->macros[0].links[0].target.devicePath == livePath);
    CHECK(live->mods[0].links[0].target.devicePath == livePath);
    CHECK(live->macros[0].links[0].target.paramIndex == 7);
    CHECK(live->mods[0].links[0].target.paramIndex == 8);
    CHECK(!live->pluginState.contains("MODIFIERASSIGNMENTS"));
}

TEST_CASE("MAGDA rack presets retarget internal macro and mod links",
          "[rack_audio][rack_presets][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Rack Preset Target");
    auto liveRackId = fixture.tm().addRackToTrack(trackId, "Live Rack");
    auto liveRackPath = ChainNodePath::rack(trackId, liveRackId);

    RackInfo presetRack;
    presetRack.id = 90;
    presetRack.name = "Preset Rack";

    ChainInfo presetChain;
    presetChain.id = 91;

    DeviceInfo presetDevice;
    presetDevice.id = 92;
    presetDevice.name = "Delay";
    presetDevice.format = PluginFormat::Internal;
    presetDevice.pluginId = "delay";
    presetDevice.pluginState = "<PLUGIN><MODIFIERASSIGNMENTS><LFO source=\"1\" paramID=\"mix\" "
                               "value=\"0.5\"/></MODIFIERASSIGNMENTS></PLUGIN>";

    auto oldTarget =
        ChainNodePath::chainDevice(123, presetRack.id, presetChain.id, presetDevice.id);

    MacroInfo rackMacro(0);
    rackMacro.links.push_back({ControlTarget::pluginParam(oldTarget, 3), 0.25f, false});
    presetRack.macros = {rackMacro};

    ModInfo deviceMod(0);
    deviceMod.links.push_back({ControlTarget::pluginParam(oldTarget, 4), 0.5f, true});
    presetDevice.mods = {deviceMod};

    presetChain.elements.push_back(makeDeviceElement(presetDevice));
    presetRack.chains.push_back(std::move(presetChain));

    REQUIRE(fixture.tm().applyRackPreset(liveRackPath, presetRack));

    auto* liveRack = fixture.tm().getRackByPath(liveRackPath);
    REQUIRE(liveRack != nullptr);
    REQUIRE(liveRack->chains.size() == 1);
    REQUIRE(liveRack->chains[0].elements.size() == 1);
    REQUIRE(isDevice(liveRack->chains[0].elements[0]));

    auto& liveDevice = getDevice(liveRack->chains[0].elements[0]);
    auto expectedTarget =
        ChainNodePath::chainDevice(trackId, liveRackId, liveRack->chains[0].id, liveDevice.id);

    REQUIRE(liveRack->macros.size() == 1);
    REQUIRE(liveRack->macros[0].links.size() == 1);
    REQUIRE(liveDevice.mods.size() == 1);
    REQUIRE(liveDevice.mods[0].links.size() == 1);

    CHECK(liveRack->macros[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveDevice.mods[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveRack->macros[0].links[0].target.paramIndex == 3);
    CHECK(liveDevice.mods[0].links[0].target.paramIndex == 4);
    CHECK(!liveDevice.pluginState.contains("MODIFIERASSIGNMENTS"));
}

TEST_CASE("MAGDA track presets retarget top-level macro and mod links",
          "[rack_audio][track_presets][macros][mods]") {
    RackAudioTestFixture fixture;

    auto trackId = fixture.tm().createTrack("Track Preset Target");

    DeviceInfo presetDevice;
    presetDevice.id = 200;
    presetDevice.name = "Delay";
    presetDevice.format = PluginFormat::Internal;
    presetDevice.pluginId = "delay";

    auto oldTarget = ChainNodePath::topLevelDevice(123, presetDevice.id);

    MacroInfo macro(0);
    macro.links.push_back({ControlTarget::pluginParam(oldTarget, 5), 0.25f, false});
    presetDevice.macros = {macro};

    ModInfo mod(0);
    mod.links.push_back({ControlTarget::pluginParam(oldTarget, 6), 0.5f, true});
    presetDevice.mods = {mod};

    std::vector<ChainElement> presetElements;
    presetElements.push_back(makeDeviceElement(presetDevice));

    REQUIRE(fixture.tm().applyChainPreset(trackId, std::move(presetElements)));

    auto* track = fixture.tm().getTrack(trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->chainElements.size() == 1);
    REQUIRE(isDevice(track->chainElements[0]));

    auto& liveDevice = getDevice(track->chainElements[0]);
    auto expectedTarget = ChainNodePath::topLevelDevice(trackId, liveDevice.id);

    REQUIRE(liveDevice.macros.size() == 1);
    REQUIRE(liveDevice.mods.size() == 1);
    REQUIRE(liveDevice.macros[0].links.size() == 1);
    REQUIRE(liveDevice.mods[0].links.size() == 1);

    CHECK(liveDevice.macros[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveDevice.mods[0].links[0].target.devicePath == expectedTarget);
    CHECK(liveDevice.macros[0].links[0].target.paramIndex == 5);
    CHECK(liveDevice.mods[0].links[0].target.paramIndex == 6);
}

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
        link.target.devicePath = ChainNodePath::topLevelDevice(0, delayId);
        link.target.paramIndex = 0;
        link.amount = 0.75f;
        rack->macros[0].links.push_back(link);

        REQUIRE(rack->macros[0].isLinked());
        REQUIRE(rack->macros[0].links.size() == 1);
        REQUIRE(rack->macros[0].links[0].target.deviceId() == delayId);
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
        link.target.devicePath = ChainNodePath::topLevelDevice(0, eqId);
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
