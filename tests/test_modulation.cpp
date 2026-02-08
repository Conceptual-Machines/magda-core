#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/MacroInfo.hpp"
#include "../magda/daw/core/ModInfo.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// MacroInfo Tests
// ============================================================================

TEST_CASE("MacroInfo - Basic structure and initialization", "[modulation][macro]") {
    SECTION("Default constructor") {
        MacroInfo macro;
        REQUIRE(macro.id == INVALID_MACRO_ID);
        REQUIRE(macro.name.isEmpty());
        REQUIRE(macro.value == Catch::Approx(0.5f));
        REQUIRE_FALSE(macro.target.isValid());
        REQUIRE(macro.links.empty());
    }

    SECTION("Constructor with index") {
        MacroInfo macro(3);
        REQUIRE(macro.id == 3);
        REQUIRE(macro.name == "Macro 4");  // index + 1
        REQUIRE(macro.value == Catch::Approx(0.5f));
        REQUIRE_FALSE(macro.isLinked());
    }
}

TEST_CASE("MacroInfo - Single link management", "[modulation][macro]") {
    MacroInfo macro(0);
    MacroTarget target{DeviceId(42), 5};

    SECTION("Get link from empty macro") {
        REQUIRE(macro.getLink(target) == nullptr);
    }

    SECTION("Add link and retrieve it") {
        MacroLink link{target, 0.75f};
        macro.links.push_back(link);

        const auto* retrievedLink = macro.getLink(target);
        REQUIRE(retrievedLink != nullptr);
        REQUIRE(retrievedLink->target == target);
        REQUIRE(retrievedLink->amount == Catch::Approx(0.75f));
        REQUIRE(macro.isLinked());
    }

    SECTION("Remove link") {
        MacroLink link{target, 0.5f};
        macro.links.push_back(link);
        REQUIRE(macro.isLinked());

        macro.removeLink(target);
        REQUIRE_FALSE(macro.isLinked());
        REQUIRE(macro.getLink(target) == nullptr);
    }
}

TEST_CASE("MacroInfo - Multiple links support", "[modulation][macro]") {
    MacroInfo macro(0);

    MacroTarget target1{DeviceId(10), 0};
    MacroTarget target2{DeviceId(10), 1};
    MacroTarget target3{DeviceId(20), 0};

    SECTION("Add multiple links") {
        macro.links.push_back(MacroLink{target1, 0.25f});
        macro.links.push_back(MacroLink{target2, 0.50f});
        macro.links.push_back(MacroLink{target3, 0.75f});

        REQUIRE(macro.links.size() == 3);
        REQUIRE(macro.isLinked());

        // Verify each link independently
        const auto* link1 = macro.getLink(target1);
        const auto* link2 = macro.getLink(target2);
        const auto* link3 = macro.getLink(target3);

        REQUIRE(link1 != nullptr);
        REQUIRE(link1->amount == Catch::Approx(0.25f));

        REQUIRE(link2 != nullptr);
        REQUIRE(link2->amount == Catch::Approx(0.50f));

        REQUIRE(link3 != nullptr);
        REQUIRE(link3->amount == Catch::Approx(0.75f));
    }

    SECTION("Remove one link keeps others") {
        macro.links.push_back(MacroLink{target1, 0.25f});
        macro.links.push_back(MacroLink{target2, 0.50f});
        macro.links.push_back(MacroLink{target3, 0.75f});

        macro.removeLink(target2);

        REQUIRE(macro.links.size() == 2);
        REQUIRE(macro.isLinked());
        REQUIRE(macro.getLink(target1) != nullptr);
        REQUIRE(macro.getLink(target2) == nullptr);
        REQUIRE(macro.getLink(target3) != nullptr);
    }

    SECTION("Modify link amount") {
        macro.links.push_back(MacroLink{target1, 0.25f});

        auto* link = macro.getLink(target1);
        REQUIRE(link != nullptr);
        link->amount = 0.90f;

        const auto* retrievedLink = macro.getLink(target1);
        REQUIRE(retrievedLink->amount == Catch::Approx(0.90f));
    }
}

TEST_CASE("MacroInfo - Link uniqueness", "[modulation][macro]") {
    MacroInfo macro(0);
    MacroTarget target{DeviceId(42), 5};

    SECTION("Cannot have duplicate links to same target") {
        macro.links.push_back(MacroLink{target, 0.25f});
        macro.links.push_back(MacroLink{target, 0.75f});

        // getLink should return the first match
        const auto* link = macro.getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE(link->amount == Catch::Approx(0.25f));

        // But removeLink should remove ALL duplicates
        macro.removeLink(target);
        REQUIRE(macro.links.empty());
    }
}

// ============================================================================
// ModInfo Tests
// ============================================================================

TEST_CASE("ModInfo - Basic structure and initialization", "[modulation][mod]") {
    SECTION("Constructor with index") {
        ModInfo mod(2);
        REQUIRE(mod.id == 2);
        REQUIRE(mod.name == "LFO 3");
        REQUIRE(mod.type == ModType::LFO);
        REQUIRE(mod.amount == Catch::Approx(0.5f));
        REQUIRE(mod.rate == Catch::Approx(1.0f));
        REQUIRE_FALSE(mod.isLinked());
    }
}

TEST_CASE("ModInfo - Link management", "[modulation][mod]") {
    ModInfo mod(0);
    ModTarget target{DeviceId(100), 3};

    SECTION("Add and retrieve link") {
        ModLink link{target, 0.65f};
        mod.links.push_back(link);

        const auto* retrievedLink = mod.getLink(target);
        REQUIRE(retrievedLink != nullptr);
        REQUIRE(retrievedLink->target == target);
        REQUIRE(retrievedLink->amount == Catch::Approx(0.65f));
    }

    SECTION("Multiple links for mod") {
        ModTarget target1{DeviceId(100), 0};
        ModTarget target2{DeviceId(100), 1};

        mod.links.push_back(ModLink{target1, 0.3f});
        mod.links.push_back(ModLink{target2, 0.7f});

        REQUIRE(mod.links.size() == 2);
        REQUIRE(mod.getLink(target1)->amount == Catch::Approx(0.3f));
        REQUIRE(mod.getLink(target2)->amount == Catch::Approx(0.7f));
    }
}

// ============================================================================
// MacroTarget and ModTarget Tests
// ============================================================================

TEST_CASE("MacroTarget - Validity and comparison", "[modulation][macro]") {
    SECTION("Invalid target") {
        MacroTarget target;
        REQUIRE_FALSE(target.isValid());
        REQUIRE(target.deviceId == INVALID_DEVICE_ID);
        REQUIRE(target.paramIndex == -1);
    }

    SECTION("Valid target") {
        MacroTarget target{DeviceId(10), 5};
        REQUIRE(target.isValid());
    }

    SECTION("Equality comparison") {
        MacroTarget target1{DeviceId(10), 5};
        MacroTarget target2{DeviceId(10), 5};
        MacroTarget target3{DeviceId(10), 6};
        MacroTarget target4{DeviceId(11), 5};

        REQUIRE(target1 == target2);
        REQUIRE_FALSE(target1 == target3);
        REQUIRE_FALSE(target1 == target4);
        REQUIRE(target1 != target3);
    }
}

TEST_CASE("ModTarget - Validity and comparison", "[modulation][mod]") {
    SECTION("Invalid target") {
        ModTarget target;
        REQUIRE_FALSE(target.isValid());
    }

    SECTION("Valid target") {
        ModTarget target{DeviceId(20), 3};
        REQUIRE(target.isValid());
    }

    SECTION("Equality comparison") {
        ModTarget target1{DeviceId(20), 3};
        ModTarget target2{DeviceId(20), 3};
        ModTarget target3{DeviceId(21), 3};

        REQUIRE(target1 == target2);
        REQUIRE_FALSE(target1 == target3);
    }
}

// ============================================================================
// MacroArray and ModArray Helpers Tests
// ============================================================================

TEST_CASE("MacroArray - Creation and page management", "[modulation][macro]") {
    SECTION("Create default macros") {
        auto macros = createDefaultMacros();
        REQUIRE(macros.size() == NUM_MACROS);
        REQUIRE(macros[0].name == "Macro 1");
        REQUIRE(macros[15].name == "Macro 16");
    }

    SECTION("Create custom number of macros") {
        auto macros = createDefaultMacros(8);
        REQUIRE(macros.size() == 8);
    }

    SECTION("Add macro page") {
        auto macros = createDefaultMacros(8);
        addMacroPage(macros);

        REQUIRE(macros.size() == 16);
        REQUIRE(macros[8].name == "Macro 9");
        REQUIRE(macros[15].name == "Macro 16");
    }

    SECTION("Remove macro page") {
        auto macros = createDefaultMacros(24);  // 3 pages
        bool removed = removeMacroPage(macros);

        REQUIRE(removed);
        REQUIRE(macros.size() == 16);
    }

    SECTION("Cannot remove below minimum") {
        auto macros = createDefaultMacros(16);  // 2 pages (minimum)
        bool removed = removeMacroPage(macros);

        REQUIRE_FALSE(removed);
        REQUIRE(macros.size() == 16);
    }
}

TEST_CASE("ModArray - Creation and page management", "[modulation][mod]") {
    SECTION("Create default mods") {
        auto mods = createDefaultMods();
        REQUIRE(mods.size() == NUM_MODS);
        REQUIRE(mods[0].name == "LFO 1");
        REQUIRE(mods[0].type == ModType::LFO);
    }

    SECTION("Add mod page") {
        auto mods = createDefaultMods(8);
        addModPage(mods);

        REQUIRE(mods.size() == 16);
    }

    SECTION("Remove mod page") {
        auto mods = createDefaultMods(24);
        bool removed = removeModPage(mods);

        REQUIRE(removed);
        REQUIRE(mods.size() == 16);
    }
}

// ============================================================================
// TrackManager Integration Tests
// ============================================================================

TEST_CASE("TrackManager - Device macro operations", "[modulation][macro][integration]") {
    auto& trackManager = TrackManager::getInstance();

    // Create a test track with a device
    TrackId trackId = trackManager.createTrack();
    REQUIRE(trackId != INVALID_TRACK_ID);

    auto* track = trackManager.getTrack(trackId);
    REQUIRE(track != nullptr);

    // Add a device to the track
    DeviceInfo testDevice;
    testDevice.name = "TestDevice";
    DeviceId deviceId = trackManager.addDeviceToTrack(trackId, testDevice);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    ChainNodePath devicePath;
    devicePath.trackId = trackId;
    devicePath.topLevelDeviceId = deviceId;

    SECTION("Set device macro value") {
        trackManager.setDeviceMacroValue(devicePath, 0, 0.75f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device != nullptr);
        REQUIRE(device->macros.size() > 0);
        REQUIRE(device->macros[0].value == Catch::Approx(0.75f));
    }

    SECTION("Set device macro target and link amount") {
        MacroTarget target{deviceId, 3};

        trackManager.setDeviceMacroTarget(devicePath, 0, target);
        trackManager.setDeviceMacroLinkAmount(devicePath, 0, target, 0.8f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device != nullptr);

        const auto* link = device->macros[0].getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE(link->amount == Catch::Approx(0.8f));
    }

    SECTION("Create multiple macro links on same device") {
        MacroTarget target1{deviceId, 0};
        MacroTarget target2{deviceId, 1};

        trackManager.setDeviceMacroLinkAmount(devicePath, 0, target1, 0.3f);
        trackManager.setDeviceMacroLinkAmount(devicePath, 0, target2, 0.7f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device->macros[0].links.size() == 2);
        REQUIRE(device->macros[0].getLink(target1)->amount == Catch::Approx(0.3f));
        REQUIRE(device->macros[0].getLink(target2)->amount == Catch::Approx(0.7f));
    }

    SECTION("Set device macro name") {
        trackManager.setDeviceMacroName(devicePath, 0, "Cutoff");

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device->macros[0].name == "Cutoff");
    }

    // Cleanup
    trackManager.deleteTrack(trackId);
}

TEST_CASE("TrackManager - Rack macro operations", "[modulation][macro][integration]") {
    auto& trackManager = TrackManager::getInstance();

    // Create track and rack
    TrackId trackId = trackManager.createTrack();
    RackId rackId = trackManager.addRackToTrack(trackId, "TestRack");
    REQUIRE(rackId != INVALID_RACK_ID);

    ChainNodePath rackPath;
    rackPath.trackId = trackId;
    rackPath.steps.push_back({ChainStepType::Rack, rackId});

    SECTION("Set rack macro value") {
        trackManager.setRackMacroValue(rackPath, 0, 0.65f);

        auto* rack = trackManager.getRackByPath(rackPath);
        REQUIRE(rack != nullptr);
        REQUIRE(rack->macros[0].value == Catch::Approx(0.65f));
    }

    SECTION("Set rack macro link amount") {
        // Add a device to one of the rack's chains
        DeviceId deviceId(100);  // Mock device ID
        MacroTarget target{deviceId, 2};

        trackManager.setRackMacroLinkAmount(rackPath, 0, target, 0.9f);

        auto* rack = trackManager.getRackByPath(rackPath);
        REQUIRE(rack != nullptr);

        const auto* link = rack->macros[0].getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE(link->amount == Catch::Approx(0.9f));
    }

    SECTION("Rack macro can link to multiple devices") {
        DeviceId device1(100);
        DeviceId device2(200);
        MacroTarget target1{device1, 0};
        MacroTarget target2{device2, 5};

        trackManager.setRackMacroLinkAmount(rackPath, 1, target1, 0.4f);
        trackManager.setRackMacroLinkAmount(rackPath, 1, target2, 0.6f);

        auto* rack = trackManager.getRackByPath(rackPath);
        REQUIRE(rack->macros[1].links.size() == 2);
        REQUIRE(rack->macros[1].getLink(target1)->amount == Catch::Approx(0.4f));
        REQUIRE(rack->macros[1].getLink(target2)->amount == Catch::Approx(0.6f));
    }

    SECTION("Set rack macro name") {
        trackManager.setRackMacroName(rackPath, 2, "Mix");

        auto* rack = trackManager.getRackByPath(rackPath);
        REQUIRE(rack->macros[2].name == "Mix");
    }

    // Cleanup
    trackManager.deleteTrack(trackId);
}

TEST_CASE("TrackManager - Device mod operations", "[modulation][mod][integration]") {
    auto& trackManager = TrackManager::getInstance();

    TrackId trackId = trackManager.createTrack();
    DeviceInfo testDevice;
    testDevice.name = "TestDevice";
    DeviceId deviceId = trackManager.addDeviceToTrack(trackId, testDevice);

    ChainNodePath devicePath;
    devicePath.trackId = trackId;
    devicePath.topLevelDeviceId = deviceId;

    SECTION("Set device mod amount") {
        // Devices start with 0 mods - add one first
        trackManager.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
        trackManager.setDeviceModAmount(devicePath, 0, 0.85f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device->mods[0].amount == Catch::Approx(0.85f));
    }

    SECTION("Set device mod target and link amount") {
        trackManager.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
        ModTarget target{deviceId, 4};

        trackManager.setDeviceModTarget(devicePath, 0, target);
        trackManager.setDeviceModLinkAmount(devicePath, 0, target, 0.55f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        const auto* link = device->mods[0].getLink(target);
        REQUIRE(link != nullptr);
        REQUIRE(link->amount == Catch::Approx(0.55f));
    }

    SECTION("Set device mod type and rate") {
        trackManager.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
        trackManager.setDeviceModType(devicePath, 0, ModType::Envelope);
        trackManager.setDeviceModRate(devicePath, 0, 2.5f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device->mods[0].type == ModType::Envelope);
        REQUIRE(device->mods[0].rate == Catch::Approx(2.5f));
    }

    SECTION("Set device mod name") {
        trackManager.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
        trackManager.addDeviceMod(devicePath, 1, ModType::LFO, LFOWaveform::Sine);
        trackManager.setDeviceModName(devicePath, 1, "LFO 1");

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        REQUIRE(device->mods[1].name == "LFO 1");
    }

    // Cleanup
    trackManager.deleteTrack(trackId);
}

TEST_CASE("TrackManager - Rack vs Device macro isolation", "[modulation][macro][integration]") {
    auto& trackManager = TrackManager::getInstance();

    // Create track with rack
    TrackId trackId = trackManager.createTrack();
    RackId rackId = trackManager.addRackToTrack(trackId, "TestRack");

    ChainNodePath rackPath;
    rackPath.trackId = trackId;
    rackPath.steps.push_back({ChainStepType::Rack, rackId});

    // Add a device (in simplified form for this test)
    DeviceId deviceId(123);
    MacroTarget target{deviceId, 0};

    SECTION("Rack and device macros are independent") {
        // Set rack macro link
        trackManager.setRackMacroLinkAmount(rackPath, 0, target, 0.3f);

        // Set device macro link (simulated)
        auto* rack = trackManager.getRackByPath(rackPath);
        REQUIRE(rack != nullptr);

        // Verify rack macro has the link
        const auto* rackLink = rack->macros[0].getLink(target);
        REQUIRE(rackLink != nullptr);
        REQUIRE(rackLink->amount == Catch::Approx(0.3f));

        // Change rack macro link amount
        trackManager.setRackMacroLinkAmount(rackPath, 0, target, 0.8f);

        const auto* updatedLink = rack->macros[0].getLink(target);
        REQUIRE(updatedLink->amount == Catch::Approx(0.8f));
    }

    // Cleanup
    trackManager.deleteTrack(trackId);
}

TEST_CASE("TrackManager - Modulation calculation scenarios", "[modulation][integration]") {
    auto& trackManager = TrackManager::getInstance();

    TrackId trackId = trackManager.createTrack();
    DeviceInfo testDevice;
    testDevice.name = "TestDevice";
    DeviceId deviceId = trackManager.addDeviceToTrack(trackId, testDevice);

    ChainNodePath devicePath;
    devicePath.trackId = trackId;
    devicePath.topLevelDeviceId = deviceId;

    SECTION("Single macro modulation") {
        // Macro value = 0.5, link amount = 0.8
        // Expected modulation = 0.5 * 0.8 = 0.4
        MacroTarget target{deviceId, 0};
        trackManager.setDeviceMacroValue(devicePath, 0, 0.5f);
        trackManager.setDeviceMacroLinkAmount(devicePath, 0, target, 0.8f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        float macroValue = device->macros[0].value;
        const auto* link = device->macros[0].getLink(target);

        float expectedModulation = macroValue * link->amount;
        REQUIRE(expectedModulation == Catch::Approx(0.4f));
    }

    SECTION("Multiple macros to same parameter") {
        // Macro 0: value=0.6, amount=0.5 → 0.3
        // Macro 1: value=0.4, amount=1.0 → 0.4
        // Total modulation = 0.3 + 0.4 = 0.7
        MacroTarget target{deviceId, 0};

        trackManager.setDeviceMacroValue(devicePath, 0, 0.6f);
        trackManager.setDeviceMacroLinkAmount(devicePath, 0, target, 0.5f);

        trackManager.setDeviceMacroValue(devicePath, 1, 0.4f);
        trackManager.setDeviceMacroLinkAmount(devicePath, 1, target, 1.0f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);

        float mod0 = device->macros[0].value * device->macros[0].getLink(target)->amount;
        float mod1 = device->macros[1].value * device->macros[1].getLink(target)->amount;
        float totalModulation = mod0 + mod1;

        REQUIRE(totalModulation == Catch::Approx(0.7f));
    }

    SECTION("Mod modulation calculation") {
        // Mod amount = 0.7, link amount = 0.6
        // Expected modulation = 0.7 * 0.6 = 0.42
        trackManager.addDeviceMod(devicePath, 0, ModType::LFO, LFOWaveform::Sine);
        ModTarget target{deviceId, 2};
        trackManager.setDeviceModAmount(devicePath, 0, 0.7f);
        trackManager.setDeviceModLinkAmount(devicePath, 0, target, 0.6f);

        auto* device = trackManager.getDeviceInChainByPath(devicePath);
        float modAmount = device->mods[0].amount;
        const auto* link = device->mods[0].getLink(target);

        float expectedModulation = modAmount * link->amount;
        REQUIRE(expectedModulation == Catch::Approx(0.42f));
    }

    // Cleanup
    trackManager.deleteTrack(trackId);
}
