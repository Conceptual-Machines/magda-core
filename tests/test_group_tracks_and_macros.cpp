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

    void macroValueChanged(TrackId trackId, ChainScope scope, int ownerId, int macroIndex,
                           float value) override {
        callCount++;
        lastTrackId = trackId;
        lastScope = scope;
        lastId = ownerId;
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
    ChainScope lastScope = ChainScope::Track;
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
        auto trackId = fixture.tm().createTrack("Inst", TrackType::Audio);
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

    SECTION("setMacroValue fires macroValueChanged") {
        fixture.tm().setMacroValue(rackPath, 0, 0.75f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastTrackId == trackId);
        REQUIRE(spy.lastScope == ChainScope::Rack);
        REQUIRE(spy.lastId == rackId);
        REQUIRE(spy.lastMacroIndex == 0);
        REQUIRE(spy.lastValue == Catch::Approx(0.75f));
    }

    SECTION("setMacroValue clamps value") {
        fixture.tm().setMacroValue(rackPath, 0, 1.5f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastValue == Catch::Approx(1.0f));

        auto* rack = fixture.tm().getRackByPath(rackPath);
        REQUIRE(rack->macros[0].value == Catch::Approx(1.0f));
    }

    SECTION("setMacroValue with invalid index does nothing") {
        fixture.tm().setMacroValue(rackPath, 99, 0.5f);
        REQUIRE(spy.callCount == 0);
    }

    SECTION("Multiple macro value changes fire separately") {
        fixture.tm().setMacroValue(rackPath, 0, 0.1f);
        fixture.tm().setMacroValue(rackPath, 1, 0.9f);

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

    SECTION("setMacroValue fires macroValueChanged") {
        fixture.tm().setMacroValue(devicePath, 0, 0.3f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastTrackId == trackId);
        REQUIRE(spy.lastScope == ChainScope::Device);
        REQUIRE(spy.lastId == deviceId);
        REQUIRE(spy.lastMacroIndex == 0);
        REQUIRE(spy.lastValue == Catch::Approx(0.3f));
    }

    SECTION("setMacroValue clamps value") {
        fixture.tm().setMacroValue(devicePath, 0, -0.5f);

        REQUIRE(spy.callCount == 1);
        REQUIRE(spy.lastValue == Catch::Approx(0.0f));
    }

    SECTION("setMacroValue with invalid index does nothing") {
        fixture.tm().setMacroValue(devicePath, 99, 0.5f);
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
        fixture.tm().setMacroLinkAmount(rackPath, 0, target, 0.5f);

        REQUIRE(spy.devicesChangedCount == 1);
        REQUIRE(spy.lastDevicesTrackId == trackId);
    }

    SECTION("Updating existing link fires deviceModifiersChanged") {
        // Create the link first
        fixture.tm().setMacroLinkAmount(rackPath, 0, target, 0.5f);
        spy.modifiersChangedCount = 0;
        spy.devicesChangedCount = 0;

        // Update the existing link
        fixture.tm().setMacroLinkAmount(rackPath, 0, target, 0.8f);

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
        fixture.tm().setMacroLinkAmount(devicePath, 0, target, 0.5f);

        REQUIRE(spy.devicesChangedCount == 1);
        REQUIRE(spy.lastDevicesTrackId == trackId);
    }

    SECTION("Updating existing device macro link fires deviceModifiersChanged") {
        fixture.tm().setMacroLinkAmount(devicePath, 0, target, 0.5f);
        spy.modifiersChangedCount = 0;
        spy.devicesChangedCount = 0;

        fixture.tm().setMacroLinkAmount(devicePath, 0, target, 0.9f);

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

    SECTION("setMacroTarget with new target fires deviceModifiersChanged") {
        MacroTarget target{deviceId, 2};
        fixture.tm().setMacroTarget(devicePath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setMacroTarget with existing target does not fire") {
        MacroTarget target{deviceId, 2};
        fixture.tm().setMacroTarget(devicePath, 0, target);
        spy.modifiersChangedCount = 0;

        // Same target again — link already exists, should not fire
        fixture.tm().setMacroTarget(devicePath, 0, target);
        REQUIRE(spy.modifiersChangedCount == 0);
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
    fixture.tm().addMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    // Reset spy counters after setup
    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setModRate fires deviceModifiersChanged") {
        fixture.tm().setModRate(devicePath, 0, 2.5f);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);
    }

    SECTION("setModWaveform fires deviceModifiersChanged") {
        fixture.tm().setModWaveform(devicePath, 0, LFOWaveform::Square);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].waveform == LFOWaveform::Square);
    }

    SECTION("setModTempoSync fires deviceModifiersChanged") {
        fixture.tm().setModTempoSync(devicePath, 0, true);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].tempoSync == true);
    }

    SECTION("setModSyncDivision fires deviceModifiersChanged") {
        fixture.tm().setModSyncDivision(devicePath, 0, SyncDivision::Quarter);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModTriggerMode fires deviceModifiersChanged") {
        fixture.tm().setModTriggerMode(devicePath, 0, LFOTriggerMode::MIDI);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].triggerMode == LFOTriggerMode::MIDI);
    }

    SECTION("setModPhaseOffset fires deviceModifiersChanged") {
        fixture.tm().setModPhaseOffset(devicePath, 0, 0.25f);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].phaseOffset == Catch::Approx(0.25f));
    }

    SECTION("setModPhaseOffset clamps to 0-1") {
        fixture.tm().setModPhaseOffset(devicePath, 0, 1.5f);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].phaseOffset == Catch::Approx(1.0f));
    }

    SECTION("setModAmount does NOT fire notification") {
        fixture.tm().setModAmount(devicePath, 0, 0.85f);

        // Amount changes are silent — no UI rebuild needed
        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].amount == Catch::Approx(0.85f));
    }

    SECTION("setModAmount clamps to -1 to 1") {
        fixture.tm().setModAmount(devicePath, 0, -0.5f);
        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].amount == Catch::Approx(-0.5f));

        fixture.tm().setModAmount(devicePath, 0, -1.5f);
        dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].amount == Catch::Approx(-1.0f));
    }

    SECTION("setModName does NOT fire notification") {
        fixture.tm().setModName(devicePath, 0, "My LFO");

        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].name == "My LFO");
    }

    SECTION("setModCurvePreset fires modifiers notification") {
        fixture.tm().setModCurvePreset(devicePath, 0, CurvePreset::Exponential);

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

    fixture.tm().addMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.devicesChangedCount = 0;
    spy.modifiersChangedCount = 0;

    SECTION("setModType fires trackDevicesChanged") {
        fixture.tm().setModType(devicePath, 0, ModType::Envelope);

        REQUIRE(spy.devicesChangedCount == 1);
        REQUIRE(spy.lastDevicesTrackId == trackId);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].type == ModType::Envelope);
    }

    SECTION("setModEnabled fires trackDevicesChanged") {
        fixture.tm().setModEnabled(devicePath, 0, false);

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

    fixture.tm().addMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setModTarget fires deviceModifiersChanged") {
        ModTarget target{deviceId, 3};
        fixture.tm().setModTarget(devicePath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].target == target);
    }

    SECTION("setModTarget creates link automatically") {
        ModTarget target{deviceId, 3};
        fixture.tm().setModTarget(devicePath, 0, target);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target) != nullptr);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.0f));
    }

    SECTION("removeModLink fires deviceModifiersChanged") {
        ModTarget target{deviceId, 3};
        fixture.tm().setModTarget(devicePath, 0, target);
        spy.modifiersChangedCount = 0;

        fixture.tm().removeModLink(devicePath, 0, target);

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

    fixture.tm().addMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);

    ModTarget target{deviceId, 2};

    spy.modifiersChangedCount = 0;

    SECTION("setModLinkAmount creates link and fires") {
        fixture.tm().setModLinkAmount(devicePath, 0, target, 0.7f);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target) != nullptr);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.7f));
    }

    SECTION("setModLinkAmount updates existing link") {
        fixture.tm().setModLinkAmount(devicePath, 0, target, 0.3f);
        spy.modifiersChangedCount = 0;

        fixture.tm().setModLinkAmount(devicePath, 0, target, 0.9f);

        REQUIRE(spy.modifiersChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->mods[0].getLink(target)->amount == Catch::Approx(0.9f));
    }

    SECTION("Multiple mod links to different params") {
        ModTarget target2{deviceId, 5};

        fixture.tm().setModLinkAmount(devicePath, 0, target, 0.4f);
        fixture.tm().setModLinkAmount(devicePath, 0, target2, 0.6f);

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

    fixture.tm().addMod(rackPath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    SECTION("setModRate fires deviceModifiersChanged") {
        fixture.tm().setModRate(rackPath, 0, 3.0f);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.lastModifiersTrackId == trackId);
    }

    SECTION("setModWaveform fires deviceModifiersChanged") {
        fixture.tm().setModWaveform(rackPath, 0, LFOWaveform::Triangle);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModTempoSync fires deviceModifiersChanged") {
        fixture.tm().setModTempoSync(rackPath, 0, true);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModSyncDivision fires deviceModifiersChanged") {
        fixture.tm().setModSyncDivision(rackPath, 0, SyncDivision::Eighth);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModTriggerMode fires deviceModifiersChanged") {
        fixture.tm().setModTriggerMode(rackPath, 0, LFOTriggerMode::Transport);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModPhaseOffset fires deviceModifiersChanged") {
        fixture.tm().setModPhaseOffset(rackPath, 0, 0.5f);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModTarget fires deviceModifiersChanged") {
        ModTarget target{DeviceId(42), 0};
        fixture.tm().setModTarget(rackPath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModLinkAmount fires deviceModifiersChanged") {
        ModTarget target{DeviceId(42), 0};
        fixture.tm().setModLinkAmount(rackPath, 0, target, 0.6f);

        REQUIRE(spy.modifiersChangedCount == 1);
    }

    SECTION("setModAmount does NOT fire notification") {
        fixture.tm().setModAmount(rackPath, 0, 0.7f);

        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);
    }

    SECTION("setModName does NOT fire notification") {
        fixture.tm().setModName(rackPath, 0, "Custom LFO");

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

    fixture.tm().addMod(rackPath, 0, ModType::LFO, LFOWaveform::Sine);

    spy.devicesChangedCount = 0;

    SECTION("setModType fires trackDevicesChanged") {
        fixture.tm().setModType(rackPath, 0, ModType::Envelope);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("setModEnabled fires trackDevicesChanged") {
        fixture.tm().setModEnabled(rackPath, 0, false);

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

    SECTION("addMacroPage fires trackDevicesChanged") {
        fixture.tm().addMacroPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        // Default is NUM_MACROS (16), adding a page adds 8 more
        REQUIRE(dev->macros.size() == NUM_MACROS + 8);
    }

    SECTION("removeMacroPage fires trackDevicesChanged when page removed") {
        // Add a page first so we can remove it
        fixture.tm().addMacroPage(devicePath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeMacroPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->macros.size() == NUM_MACROS);
    }

    SECTION("removeMacroPage does not fire when at minimum") {
        fixture.tm().removeMacroPage(devicePath);

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

    SECTION("addMacroPage fires trackDevicesChanged") {
        fixture.tm().addMacroPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeMacroPage fires when page removed") {
        fixture.tm().addMacroPage(rackPath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeMacroPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeMacroPage does not fire when at minimum") {
        fixture.tm().removeMacroPage(rackPath);

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

    SECTION("addModPage fires trackDevicesChanged") {
        fixture.tm().addModPage(devicePath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeModPage fires when page removed") {
        fixture.tm().addModPage(devicePath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeModPage(devicePath);

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

    SECTION("addModPage fires trackDevicesChanged") {
        fixture.tm().addModPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    SECTION("removeModPage fires when page removed") {
        fixture.tm().addModPage(rackPath);
        spy.devicesChangedCount = 0;

        fixture.tm().removeModPage(rackPath);

        REQUIRE(spy.devicesChangedCount == 1);
    }

    fixture.tm().removeListener(&spy);
}

// ============================================================================
// Rack Macro Target Notification
// ============================================================================

TEST_CASE("Rack macro target fires deviceModifiersChanged", "[macro][notification][rack]") {
    GroupMacroTestFixture fixture;
    MacroListenerSpy spy;
    fixture.tm().addListener(&spy);

    auto trackId = fixture.tm().createTrack("Test Track");
    auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");
    auto rackPath = ChainNodePath::rack(trackId, rackId);

    spy.modifiersChangedCount = 0;
    spy.devicesChangedCount = 0;

    // Step 4 converged the per-scope notification divergence: a new macro
    // target on any scope (Track/Rack/Device) now fires the lighter
    // deviceModifiersChanged. The pre-step-4 rack path fired the heavier
    // trackDevicesChanged.
    SECTION("setMacroTarget fires deviceModifiersChanged") {
        MacroTarget target{DeviceId(42), 0};
        fixture.tm().setMacroTarget(rackPath, 0, target);

        REQUIRE(spy.modifiersChangedCount == 1);
        REQUIRE(spy.devicesChangedCount == 0);
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

    SECTION("setMacroName does not fire") {
        fixture.tm().setMacroName(rackPath, 0, "Cutoff");

        REQUIRE(spy.callCount == 0);
        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* rack = fixture.tm().getRackByPath(rackPath);
        REQUIRE(rack->macros[0].name == "Cutoff");
    }

    SECTION("setMacroName does not fire") {
        fixture.tm().setMacroName(devicePath, 0, "Filter");

        REQUIRE(spy.callCount == 0);
        REQUIRE(spy.modifiersChangedCount == 0);
        REQUIRE(spy.devicesChangedCount == 0);

        auto* dev = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(dev->macros[0].name == "Filter");
    }

    fixture.tm().removeListener(&spy);
}
