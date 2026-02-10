#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/MacroInfo.hpp"
#include "../magda/daw/core/ModInfo.hpp"
#include "../magda/daw/core/RackInfo.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture
// ============================================================================

class GroupMacroTestFixture {
  public:
    GroupMacroTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    ~GroupMacroTestFixture() {
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }
};

// ============================================================================
// Listener Spy for macroValueChanged notifications
// ============================================================================

class MacroListenerSpy : public TrackManagerListener {
  public:
    void tracksChanged() override {}

    void macroValueChanged(TrackId trackId, bool isRack, int id, int macroIndex,
                           float value) override {
        callCount++;
        lastTrackId = trackId;
        lastIsRack = isRack;
        lastId = id;
        lastMacroIndex = macroIndex;
        lastValue = value;
    }

    void deviceModifiersChanged(TrackId trackId) override {
        modifiersChangedCount++;
        lastModifiersTrackId = trackId;
    }

    void trackDevicesChanged(TrackId trackId) override {
        devicesChangedCount++;
        lastDevicesTrackId = trackId;
    }

    int callCount = 0;
    TrackId lastTrackId = INVALID_TRACK_ID;
    bool lastIsRack = false;
    int lastId = -1;
    int lastMacroIndex = -1;
    float lastValue = -1.0f;

    int modifiersChangedCount = 0;
    TrackId lastModifiersTrackId = INVALID_TRACK_ID;

    int devicesChangedCount = 0;
    TrackId lastDevicesTrackId = INVALID_TRACK_ID;
};

// ============================================================================
// Group Track: Instrument Restriction
// ============================================================================

TEST_CASE("Group track rejects instrument plugins", "[group_track][instrument]") {
    GroupMacroTestFixture fixture;

    auto groupId = fixture.tm().createGroupTrack("My Group");
    REQUIRE(groupId != INVALID_TRACK_ID);

    auto* group = fixture.tm().getTrack(groupId);
    REQUIRE(group != nullptr);
    REQUIRE(group->type == TrackType::Group);

    DeviceInfo instrument;
    instrument.name = "Synth";
    instrument.format = PluginFormat::Internal;
    instrument.pluginId = "4osc";
    instrument.isInstrument = true;

    DeviceInfo effect;
    effect.name = "Delay";
    effect.format = PluginFormat::Internal;
    effect.pluginId = "delay";
    effect.isInstrument = false;

    SECTION("addDeviceToTrack rejects instrument") {
        auto id = fixture.tm().addDeviceToTrack(groupId, instrument);
        REQUIRE(id == INVALID_DEVICE_ID);

        group = fixture.tm().getTrack(groupId);
        REQUIRE(group->chainElements.empty());
    }

    SECTION("addDeviceToTrack with index rejects instrument") {
        // Add an effect first so we have a valid insert index
        auto effectId = fixture.tm().addDeviceToTrack(groupId, effect);
        REQUIRE(effectId != INVALID_DEVICE_ID);

        auto id = fixture.tm().addDeviceToTrack(groupId, instrument, 0);
        REQUIRE(id == INVALID_DEVICE_ID);

        group = fixture.tm().getTrack(groupId);
        REQUIRE(group->chainElements.size() == 1);  // Only the effect
    }

    SECTION("addDeviceToTrack allows effects on group track") {
        auto id = fixture.tm().addDeviceToTrack(groupId, effect);
        REQUIRE(id != INVALID_DEVICE_ID);

        group = fixture.tm().getTrack(groupId);
        REQUIRE(group->chainElements.size() == 1);
    }
}

TEST_CASE("Group track rejects instruments inside rack chains", "[group_track][instrument][rack]") {
    GroupMacroTestFixture fixture;

    auto groupId = fixture.tm().createGroupTrack("My Group");
    auto rackId = fixture.tm().addRackToTrack(groupId, "FX Rack");

    auto* rack = fixture.tm().getRack(groupId, rackId);
    REQUIRE(rack != nullptr);
    auto chainId = rack->chains[0].id;

    DeviceInfo instrument;
    instrument.name = "Synth";
    instrument.format = PluginFormat::Internal;
    instrument.pluginId = "4osc";
    instrument.isInstrument = true;

    DeviceInfo effect;
    effect.name = "Delay";
    effect.format = PluginFormat::Internal;
    effect.pluginId = "delay";
    effect.isInstrument = false;

    SECTION("addDeviceToChain rejects instrument") {
        auto id = fixture.tm().addDeviceToChain(groupId, rackId, chainId, instrument);
        REQUIRE(id == INVALID_DEVICE_ID);
    }

    SECTION("addDeviceToChainByPath rejects instrument") {
        auto chainPath = ChainNodePath::chain(groupId, rackId, chainId);
        auto id = fixture.tm().addDeviceToChainByPath(chainPath, instrument);
        REQUIRE(id == INVALID_DEVICE_ID);
    }

    SECTION("addDeviceToChainByPath with index rejects instrument") {
        auto chainPath = ChainNodePath::chain(groupId, rackId, chainId);
        auto id = fixture.tm().addDeviceToChainByPath(chainPath, instrument, 0);
        REQUIRE(id == INVALID_DEVICE_ID);
    }

    SECTION("addDeviceToChainByPath allows effects") {
        auto chainPath = ChainNodePath::chain(groupId, rackId, chainId);
        auto id = fixture.tm().addDeviceToChainByPath(chainPath, effect);
        REQUIRE(id != INVALID_DEVICE_ID);
    }
}

TEST_CASE("Audio and Instrument tracks accept instruments", "[group_track][instrument]") {
    GroupMacroTestFixture fixture;

    DeviceInfo instrument;
    instrument.name = "Synth";
    instrument.format = PluginFormat::Internal;
    instrument.pluginId = "4osc";
    instrument.isInstrument = true;

    SECTION("Audio track accepts instrument") {
        auto trackId = fixture.tm().createTrack("Audio", TrackType::Audio);
        auto id = fixture.tm().addDeviceToTrack(trackId, instrument);
        REQUIRE(id != INVALID_DEVICE_ID);
    }

    SECTION("Instrument track accepts instrument") {
        auto trackId = fixture.tm().createTrack("Inst", TrackType::Instrument);
        auto id = fixture.tm().addDeviceToTrack(trackId, instrument);
        REQUIRE(id != INVALID_DEVICE_ID);
    }

    SECTION("Aux track rejects instrument") {
        auto trackId = fixture.tm().createTrack("Aux", TrackType::Aux);
        auto id = fixture.tm().addDeviceToTrack(trackId, instrument);
        REQUIRE(id == INVALID_DEVICE_ID);
    }
}

// ============================================================================
// Macro Value Changed Notifications
// ============================================================================

TEST_CASE("Rack macro value change fires notification", "[macro][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    // Reset spy after track/rack creation notifications
    spy.callCount = 0;

    SECTION("setRackMacroValue fires macroValueChanged") {
        fixture.tm().setRackMacroValue(rackPath, 0, 0.75f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastTrackId == trackId);
        REQUIRE(spy.lastIsRack == true);
        REQUIRE(spy.lastId == rackId);
        REQUIRE(spy.lastMacroIndex == 0);
        REQUIRE(spy.lastValue == Catch::Approx(0.75f));
    }

    SECTION("setRackMacroValue clamps value") {
        fixture.tm().setRackMacroValue(rackPath, 0, 1.5f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastValue == Catch::Approx(1.0f));

        auto* rack = fixture.tm().getRackByPath(rackPath);
        REQUIRE(rack->macros[0].value == Catch::Approx(1.0f));
    }

    SECTION("setRackMacroValue with invalid index does nothing") {
        fixture.tm().setRackMacroValue(rackPath, 99, 0.5f);
        REQUIRE(spy.callCount == 0);
    }

    SECTION("Multiple macro value changes fire separately") {
        fixture.tm().setRackMacroValue(rackPath, 0, 0.1f);
        fixture.tm().setRackMacroValue(rackPath, 1, 0.9f);

        REQUIRE(spy.callCount == 2);
        REQUIRE(spy.lastMacroIndex == 1);
        REQUIRE(spy.lastValue == Catch::Approx(0.9f));
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Device macro value change fires notification", "[macro][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    // Reset spy after creation notifications
    spy.callCount = 0;

    SECTION("setDeviceMacroValue fires macroValueChanged") {
        fixture.tm().setDeviceMacroValue(devicePath, 0, 0.3f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastTrackId == trackId);
        REQUIRE(spy.lastIsRack == false);
        REQUIRE(spy.lastId == deviceId);
        REQUIRE(spy.lastMacroIndex == 0);
        REQUIRE(spy.lastValue == Catch::Approx(0.3f));
    }

    SECTION("setDeviceMacroValue clamps value") {
        fixture.tm().setDeviceMacroValue(devicePath, 0, -0.5f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastValue == Catch::Approx(0.0f));
    }

    SECTION("setDeviceMacroValue with invalid index does nothing") {
        fixture.tm().setDeviceMacroValue(devicePath, 99, 0.5f);
        REQUIRE(spy.callCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Macro Link Amount Notifications
// ============================================================================

TEST_CASE("Rack macro link amount change fires modifiers notification", "[macro][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    // Add a device inside the rack so we have a valid target
    auto* rack = fixture.tm().getRackByPath(rackPath);
    auto chainId = rack->chains[0].id;
    auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

    DeviceInfo delay;
    delay.name = "Delay";
    delay.format = PluginFormat::Internal;
    delay.pluginId = "delay";
    auto delayId = fixture.tm().addDeviceToChainByPath(chainPath, delay);

    MacroTarget target{delayId, 0};

    // Reset spy counters
    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("New link fires trackDevicesChanged") {
        fixture.tm().setRackMacroLinkAmount(rackPath, 0, target, 0.5f);

        REQUIRE(spy.devicesChangedCount == 1);
        REQUIRE(spy.lastDevicesTrackId == trackId);
    }

    SECTION("Updating existing link fires deviceModifiersChanged") {
        // Create the link first
        fixture.tm().setRackMacroLinkAmount(rackPath, 0, target, 0.5f);
        spy.modifiersChangedCount = 0;
        spy.devicesChangedCount = 0;

        // Update the existing link
        fixture.tm().setRackMacroLinkAmount(rackPath, 0, target, 0.8f);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);
        // Should NOT fire trackDevicesChanged for an amount-only change
        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Device macro link amount change fires notifications", "[macro][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    MacroTarget target{deviceId, 0};

    // Reset spy counters
    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("New device macro link fires trackDevicesChanged") {
        fixture.tm().setDeviceMacroLinkAmount(devicePath, 0, target, 0.5f);

        REQUIRE(spy.devicesChangedCount == 1);
        REQUIRE(spy.lastDevicesTrackId == trackId);
    }

    SECTION("Updating existing device macro link fires deviceModifiersChanged") {
        fixture.tm().setDeviceMacroLinkAmount(devicePath, 0, target, 0.5f);
        spy.modifiersChangedCount = 0;
        spy.devicesChangedCount = 0;

        fixture.tm().setDeviceMacroLinkAmount(devicePath, 0, target, 0.9f);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);
        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Device macro target fires trackDevicesChanged", "[macro][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    // Reset spy counters
    spy.devicesChangedCount = 0;

    SECTION("setDeviceMacroTarget with new target fires trackDevicesChanged") {
        MacroTarget target{deviceId, 2};
        fixture.tm().setDeviceMacroTarget(devicePath, 0, target);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("setDeviceMacroTarget with existing target does not fire") {
        MacroTarget target{deviceId, 2};
        fixture.tm().setDeviceMacroTarget(devicePath, 0, target);
        spy.devicesChangedCount = 0;

        // Same target again — link already exists, should not fire
        fixture.tm().setDeviceMacroTarget(devicePath, 0, target);
        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Device Mod Property Notifications
// ============================================================================

TEST_CASE("Device mod property changes fire deviceModifiersChanged", "[mod][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    // Add a mod so we have something to modify
    fixture.tm().addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    // Reset spy counters after setup
    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setDeviceModRate fires deviceModifiersChanged") {
        fixture.tm().setDeviceModRate(devicePath, 0, 2.5f);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);
    }

    SECTION("setDeviceModWaveform fires deviceModifiersChanged") {
        fixture.tm().setDeviceModWaveform(devicePath, 0, LFOWaveform::Square);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].waveform == LFOWaveform::Square);
    }

    SECTION("setDeviceModTempoSync fires deviceModifiersChanged") {
        fixture.tm().setDeviceModTempoSync(devicePath, 0, true);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].tempoSync == true);
    }

    SECTION("setDeviceModSyncDivision fires deviceModifiersChanged") {
        fixture.tm().setDeviceModSyncDivision(devicePath, 0, SyncDivision::Quarter);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setDeviceModTriggerMode fires deviceModifiersChanged") {
        fixture.tm().setDeviceModTriggerMode(devicePath, 0, LFOTriggerMode::MIDI);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].triggerMode == LFOTriggerMode::MIDI);
    }

    SECTION("setDeviceModPhaseOffset fires deviceModifiersChanged") {
        fixture.tm().setDeviceModPhaseOffset(devicePath, 0, 0.25f);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].phaseOffset == Catch::Approx(0.25f));
    }

    SECTION("setDeviceModPhaseOffset clamps to 0-1") {
        fixture.tm().setDeviceModPhaseOffset(devicePath, 0, 1.5f);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].phaseOffset == Catch::Approx(1.0f));
    }

    SECTION("setDeviceModAmount does NOT fire notification") {
        fixture.tm().setDeviceModAmount(devicePath, 0, 0.85f);

        // Amount changes are silent — no UI rebuild needed
        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].amount == Catch::Approx(0.85f));
    }

    SECTION("setDeviceModAmount clamps to 0-1") {
        fixture.tm().setDeviceModAmount(devicePath, 0, -0.5f);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].amount == Catch::Approx(0.0f));
    }

    SECTION("setDeviceModName does NOT fire notification") {
        fixture.tm().setDeviceModName(devicePath, 0, "My LFO");

        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].name == "My LFO");
    }

    SECTION("setDeviceModCurvePreset fires modifiers notification") {
        fixture.tm().setDeviceModCurvePreset(devicePath, 0, CurvePreset::Exponential);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Device mod type change fires trackDevicesChanged", "[mod][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    fixture.tm().addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.devicesChangedCount = 0;
    spy.modifiersChangedCount = 0;

    SECTION("setDeviceModType fires trackDevicesChanged") {
        fixture.tm().setDeviceModType(devicePath, 0, ModType::Envelope);

        REQUIRE(spy.devicesChangedCount == 1);
        REQUIRE(spy.lastDevicesTrackId == trackId);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].type == ModType::Envelope);
    }

    SECTION("setDeviceModEnabled fires trackDevicesChanged") {
        fixture.tm().setDeviceModEnabled(devicePath, 0, false);

        REQUIRE(spy.devicesChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].enabled == false);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Device Mod Target and Link Notifications
// ============================================================================

TEST_CASE("Device mod target fires deviceModifiersChanged", "[mod][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    fixture.tm().addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setDeviceModTarget fires deviceModifiersChanged") {
        ModTarget target{deviceId, 3};
        fixture.tm().setDeviceModTarget(devicePath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].target == target);
    }

    SECTION("setDeviceModTarget creates link automatically") {
        ModTarget target{deviceId, 3};
        fixture.tm().setDeviceModTarget(devicePath, 0, target);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target) != nullptr);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.5f));
    }

    SECTION("removeDeviceModLink fires deviceModifiersChanged") {
        ModTarget target{deviceId, 3};
        fixture.tm().setDeviceModTarget(devicePath, 0, target);
        spy.modifiersChangedCount = 0;

        fixture.tm().removeDeviceModLink(devicePath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target) == nullptr);
        // Target should also be cleared
        REQUIRE_FALSE(dev->mods[0].target.isValid());
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Device mod link amount fires deviceModifiersChanged", "[mod][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    fixture.tm().addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    ModTarget target{deviceId, 2};

    spy.modifiersChangedCount = 0;

    SECTION("setDeviceModLinkAmount creates link and fires") {
        fixture.tm().setDeviceModLinkAmount(devicePath, 0, target, 0.7f);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target) != nullptr);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.7f));
    }

    SECTION("setDeviceModLinkAmount updates existing link") {
        fixture.tm().setDeviceModLinkAmount(devicePath, 0, target, 0.3f);
        spy.modifiersChangedCount = 0;

        fixture.tm().setDeviceModLinkAmount(devicePath, 0, target, 0.9f);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.9f));
    }

    SECTION("Multiple mod links to different params") {
        ModTarget target2{deviceId, 5};

        fixture.tm().setDeviceModLinkAmount(devicePath, 0, target, 0.4f);
        fixture.tm().setDeviceModLinkAmount(devicePath, 0, target2, 0.6f);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].links.size() == 2);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.4f));
        REQUIRE(dev->mods[0].getLink(target2)->amount == Catch::Approx(0.6f));
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Rack Mod Property Notifications
// ============================================================================

TEST_CASE("Rack mod property changes fire deviceModifiersChanged", "[mod][notification][rack]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    fixture.tm().addRackMod(rackPath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setRackModRate fires deviceModifiersChanged") {
        fixture.tm().setRackModRate(rackPath, 0, 3.0f);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);
    }

    SECTION("setRackModWaveform fires deviceModifiersChanged") {
        fixture.tm().setRackModWaveform(rackPath, 0, LFOWaveform::Triangle);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModTempoSync fires deviceModifiersChanged") {
        fixture.tm().setRackModTempoSync(rackPath, 0, true);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModSyncDivision fires deviceModifiersChanged") {
        fixture.tm().setRackModSyncDivision(rackPath, 0, SyncDivision::Eighth);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModTriggerMode fires deviceModifiersChanged") {
        fixture.tm().setRackModTriggerMode(rackPath, 0, LFOTriggerMode::Transport);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModPhaseOffset fires deviceModifiersChanged") {
        fixture.tm().setRackModPhaseOffset(rackPath, 0, 0.5f);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModTarget fires deviceModifiersChanged") {
        ModTarget target{DeviceId(42), 0};
        fixture.tm().setRackModTarget(rackPath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModLinkAmount fires deviceModifiersChanged") {
        ModTarget target{DeviceId(42), 0};
        fixture.tm().setRackModLinkAmount(rackPath, 0, target, 0.6f);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setRackModAmount does NOT fire notification") {
        fixture.tm().setRackModAmount(rackPath, 0, 0.7f);

        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);
    }

    SECTION("setRackModName does NOT fire notification") {
        fixture.tm().setRackModName(rackPath, 0, "Custom LFO");

        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Rack mod type and enable change fire trackDevicesChanged", "[mod][notification][rack]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    fixture.tm().addRackMod(rackPath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.devicesChangedCount = 0;

    SECTION("setRackModType fires trackDevicesChanged") {
        fixture.tm().setRackModType(rackPath, 0, ModType::Envelope);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("setRackModEnabled fires trackDevicesChanged") {
        fixture.tm().setRackModEnabled(rackPath, 0, false);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Macro and Mod Page Management Notifications
// ============================================================================

TEST_CASE("Device macro page add/remove fires trackDevicesChanged", "[macro][notification][page]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    spy.devicesChangedCount = 0;

    SECTION("addDeviceMacroPage fires trackDevicesChanged") {
        fixture.tm().addDeviceMacroPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        // Default is NUM_MACROS (16), adding a page adds 8 more
        REQUIRE(dev->macros.size() == NUM_MACROS + 8);
    }

    SECTION("removeDeviceMacroPage fires trackDevicesChanged when page removed") {
        // Add a page first so we can remove it
        fixture.tm().addDeviceMacroPage(devicePath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeDeviceMacroPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->macros.size() == NUM_MACROS);
    }

    SECTION("removeDeviceMacroPage does not fire when at minimum") {
        fixture.tm().removeDeviceMacroPage(devicePath);

        // Should not fire - already at minimum
        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Rack macro page add/remove fires trackDevicesChanged", "[macro][notification][page]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    spy.devicesChangedCount = 0;

    SECTION("addRackMacroPage fires trackDevicesChanged") {
        fixture.tm().addRackMacroPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeRackMacroPage fires when page removed") {
        fixture.tm().addRackMacroPage(rackPath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeRackMacroPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeRackMacroPage does not fire when at minimum") {
        fixture.tm().removeRackMacroPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 0);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Device mod page add/remove fires trackDevicesChanged", "[mod][notification][page]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    spy.devicesChangedCount = 0;

    SECTION("addDeviceModPage fires trackDevicesChanged") {
        fixture.tm().addDeviceModPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeDeviceModPage fires when page removed") {
        fixture.tm().addDeviceModPage(devicePath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeDeviceModPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    fixture.tm().removeListener(&spy);
}

TEST_CASE("Rack mod page add/remove fires trackDevicesChanged", "[mod][notification][page]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    spy.devicesChangedCount = 0;

    SECTION("addRackModPage fires trackDevicesChanged") {
        fixture.tm().addRackModPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeRackModPage fires when page removed") {
        fixture.tm().addRackModPage(rackPath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeRackModPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Rack Macro Target Notification
// ============================================================================

TEST_CASE("Rack macro target fires trackDevicesChanged", "[macro][notification][rack]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    spy.devicesChangedCount = 0;

    SECTION("setRackMacroTarget fires trackDevicesChanged") {
        MacroTarget target{DeviceId(42), 0};
        fixture.tm().setRackMacroTarget(rackPath, 0, target);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Macro Name Changes (silent — no notification)
// ============================================================================

TEST_CASE("Macro name changes are silent", "[macro][notification]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    DeviceInfo device;
    device.name = "TestDevice";
    auto deviceId = fixture.tm().addDeviceToTrack(trackId, device);
    auto devicePath = ChainNodePath::topLevelDevice(trackId, deviceId);

    spy.callCount = 0;
    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setRackMacroName does not fire") {
        fixture.tm().setRackMacroName(rackPath, 0, "Cutoff");

        REQUIRE(spy.callCount == 0);
        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* rack = fixture.tm().getRackByPath(rackPath);
        REQUIRE(rack->macros[0].name == "Cutoff");
    }

    SECTION("setDeviceMacroName does not fire") {
        fixture.tm().setDeviceMacroName(devicePath, 0, "Filter");

        REQUIRE(spy.callCount == 0);
        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->macros[0].name == "Filter");
    }

    fixture.tm().removeListener(&spy);
}
