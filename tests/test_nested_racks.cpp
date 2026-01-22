#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/RackInfo.hpp"
#include "../magda/daw/core/SelectionManager.hpp"
#include "../magda/daw/core/TrackManager.hpp"

using namespace magda;

// ============================================================================
// Test Fixture Helper
// ============================================================================

/**
 * Helper class to manage TrackManager state for tests.
 * Clears all tracks on destruction to prevent test interference.
 */
class TrackManagerTestFixture {
  public:
    TrackManagerTestFixture() {
        // Clear existing tracks for a clean test environment
        TrackManager::getInstance().clearAllTracks();
    }

    ~TrackManagerTestFixture() {
        // Clean up after test
        TrackManager::getInstance().clearAllTracks();
    }

    TrackManager& tm() {
        return TrackManager::getInstance();
    }
};

// ============================================================================
// ChainNodePath Construction Tests
// ============================================================================

TEST_CASE("ChainNodePath Factory Methods", "[chain_path][construction]") {
    SECTION("Create empty path") {
        ChainNodePath path;
        REQUIRE(path.trackId == INVALID_TRACK_ID);
        REQUIRE(path.steps.empty());
        REQUIRE(path.getType() == ChainNodeType::None);
        REQUIRE_FALSE(path.isValid());
    }

    SECTION("Create top-level device path") {
        auto path = ChainNodePath::topLevelDevice(1, 42);
        REQUIRE(path.trackId == 1);
        REQUIRE(path.topLevelDeviceId == 42);
        REQUIRE(path.steps.empty());
        REQUIRE(path.getType() == ChainNodeType::TopLevelDevice);
        REQUIRE(path.isValid());
        REQUIRE(path.getDeviceId() == 42);
    }

    SECTION("Create rack path") {
        auto path = ChainNodePath::rack(1, 10);
        REQUIRE(path.trackId == 1);
        REQUIRE(path.steps.size() == 1);
        REQUIRE(path.steps[0].type == ChainStepType::Rack);
        REQUIRE(path.steps[0].id == 10);
        REQUIRE(path.getType() == ChainNodeType::Rack);
        REQUIRE(path.isValid());
        REQUIRE(path.getRackId() == 10);
    }

    SECTION("Create chain path") {
        auto path = ChainNodePath::chain(1, 10, 20);
        REQUIRE(path.trackId == 1);
        REQUIRE(path.steps.size() == 2);
        REQUIRE(path.steps[0].type == ChainStepType::Rack);
        REQUIRE(path.steps[0].id == 10);
        REQUIRE(path.steps[1].type == ChainStepType::Chain);
        REQUIRE(path.steps[1].id == 20);
        REQUIRE(path.getType() == ChainNodeType::Chain);
        REQUIRE(path.isValid());
        REQUIRE(path.getRackId() == 10);
        REQUIRE(path.getChainId() == 20);
    }

    SECTION("Create chain device path") {
        auto path = ChainNodePath::chainDevice(1, 10, 20, 30);
        REQUIRE(path.trackId == 1);
        REQUIRE(path.steps.size() == 3);
        REQUIRE(path.steps[0].type == ChainStepType::Rack);
        REQUIRE(path.steps[1].type == ChainStepType::Chain);
        REQUIRE(path.steps[2].type == ChainStepType::Device);
        REQUIRE(path.getType() == ChainNodeType::Device);
        REQUIRE(path.isValid());
        REQUIRE(path.getDeviceId() == 30);
    }
}

TEST_CASE("ChainNodePath Extension Methods", "[chain_path][extension]") {
    SECTION("withRack extends path") {
        auto rackPath = ChainNodePath::rack(1, 10);
        auto chainPath = rackPath.withChain(20);
        auto nestedRackPath = chainPath.withRack(30);

        REQUIRE(nestedRackPath.steps.size() == 3);
        REQUIRE(nestedRackPath.steps[0].type == ChainStepType::Rack);
        REQUIRE(nestedRackPath.steps[0].id == 10);
        REQUIRE(nestedRackPath.steps[1].type == ChainStepType::Chain);
        REQUIRE(nestedRackPath.steps[1].id == 20);
        REQUIRE(nestedRackPath.steps[2].type == ChainStepType::Rack);
        REQUIRE(nestedRackPath.steps[2].id == 30);
        REQUIRE(nestedRackPath.getType() == ChainNodeType::Rack);
    }

    SECTION("withDevice extends path") {
        auto chainPath = ChainNodePath::chain(1, 10, 20);
        auto devicePath = chainPath.withDevice(99);

        REQUIRE(devicePath.steps.size() == 3);
        REQUIRE(devicePath.steps[2].type == ChainStepType::Device);
        REQUIRE(devicePath.steps[2].id == 99);
        REQUIRE(devicePath.getType() == ChainNodeType::Device);
        REQUIRE(devicePath.getDeviceId() == 99);
    }

    SECTION("Deep nesting path construction") {
        // Track > Rack[1] > Chain[2] > Rack[3] > Chain[4] > Device[5]
        auto path = ChainNodePath::rack(1, 1).withChain(2).withRack(3).withChain(4).withDevice(5);

        REQUIRE(path.steps.size() == 5);
        REQUIRE(path.depth() == 5);
        REQUIRE(path.getType() == ChainNodeType::Device);
        REQUIRE(path.getDeviceId() == 5);
    }
}

TEST_CASE("ChainNodePath Parent Method", "[chain_path][parent]") {
    SECTION("Parent of device path is chain path") {
        auto devicePath = ChainNodePath::chainDevice(1, 10, 20, 30);
        auto parentPath = devicePath.parent();

        REQUIRE(parentPath.steps.size() == 2);
        REQUIRE(parentPath.getType() == ChainNodeType::Chain);
    }

    SECTION("Parent of chain path is rack path") {
        auto chainPath = ChainNodePath::chain(1, 10, 20);
        auto parentPath = chainPath.parent();

        REQUIRE(parentPath.steps.size() == 1);
        REQUIRE(parentPath.getType() == ChainNodeType::Rack);
    }

    SECTION("Parent of rack path has no steps") {
        auto rackPath = ChainNodePath::rack(1, 10);
        auto parentPath = rackPath.parent();

        REQUIRE(parentPath.steps.empty());
        REQUIRE(parentPath.trackId == 1);
    }

    SECTION("Parent of deeply nested path") {
        auto deepPath =
            ChainNodePath::rack(1, 1).withChain(2).withRack(3).withChain(4).withDevice(5);
        auto parent1 = deepPath.parent();  // Chain[4]
        auto parent2 = parent1.parent();   // Rack[3]
        auto parent3 = parent2.parent();   // Chain[2]
        auto parent4 = parent3.parent();   // Rack[1]
        auto parent5 = parent4.parent();   // Empty

        REQUIRE(parent1.getType() == ChainNodeType::Chain);
        REQUIRE(parent2.getType() == ChainNodeType::Rack);
        REQUIRE(parent3.getType() == ChainNodeType::Chain);
        REQUIRE(parent4.getType() == ChainNodeType::Rack);
        REQUIRE(parent5.steps.empty());
    }
}

TEST_CASE("ChainNodePath Equality", "[chain_path][equality]") {
    SECTION("Equal paths") {
        auto path1 = ChainNodePath::chainDevice(1, 10, 20, 30);
        auto path2 = ChainNodePath::chainDevice(1, 10, 20, 30);
        REQUIRE(path1 == path2);
    }

    SECTION("Different track IDs") {
        auto path1 = ChainNodePath::rack(1, 10);
        auto path2 = ChainNodePath::rack(2, 10);
        REQUIRE(path1 != path2);
    }

    SECTION("Different path lengths") {
        auto path1 = ChainNodePath::rack(1, 10);
        auto path2 = ChainNodePath::chain(1, 10, 20);
        REQUIRE(path1 != path2);
    }

    SECTION("Different IDs in path") {
        auto path1 = ChainNodePath::chain(1, 10, 20);
        auto path2 = ChainNodePath::chain(1, 10, 21);
        REQUIRE(path1 != path2);
    }
}

TEST_CASE("ChainNodePath toString", "[chain_path][debug]") {
    SECTION("Rack path to string") {
        auto path = ChainNodePath::rack(1, 10);
        auto str = path.toString();
        REQUIRE(str.contains("Track[1]"));
        REQUIRE(str.contains("Rack[10]"));
    }

    SECTION("Deep path to string") {
        auto path = ChainNodePath::rack(1, 10).withChain(20).withRack(30).withDevice(40);
        auto str = path.toString();
        REQUIRE(str.contains("Track[1]"));
        REQUIRE(str.contains("Rack[10]"));
        REQUIRE(str.contains("Chain[20]"));
        REQUIRE(str.contains("Rack[30]"));
        REQUIRE(str.contains("Device[40]"));
    }
}

// ============================================================================
// TrackManager Rack Operations Tests
// ============================================================================

TEST_CASE("TrackManager: Add Rack to Track", "[trackmanager][rack]") {
    TrackManagerTestFixture fixture;

    SECTION("Add single rack to track") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        REQUIRE(rackId != INVALID_RACK_ID);

        const auto* racks = fixture.tm().getRacks(trackId);
        REQUIRE(racks != nullptr);
        REQUIRE(racks->size() == 1);
        REQUIRE((*racks)[0].id == rackId);
        REQUIRE((*racks)[0].name == "Test Rack");
    }

    SECTION("Add multiple racks to track") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rack1 = fixture.tm().addRackToTrack(trackId, "Rack 1");
        auto rack2 = fixture.tm().addRackToTrack(trackId, "Rack 2");

        const auto* racks = fixture.tm().getRacks(trackId);
        REQUIRE(racks->size() == 2);
        REQUIRE(rack1 != rack2);
    }

    SECTION("New rack has default chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack != nullptr);
        REQUIRE(rack->chains.size() == 1);
        REQUIRE(rack->chains[0].name == "Chain 1");
    }
}

TEST_CASE("TrackManager: getRackByPath", "[trackmanager][rack][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Get top-level rack by path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto path = ChainNodePath::rack(trackId, rackId);
        auto* rack = fixture.tm().getRackByPath(path);

        REQUIRE(rack != nullptr);
        REQUIRE(rack->id == rackId);
        REQUIRE(rack->name == "Test Rack");
    }

    SECTION("Get nested rack by path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* topRack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(topRack != nullptr);
        auto chainId = topRack->chains[0].id;

        // Add nested rack
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);
        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");

        // Get nested rack by path
        auto nestedPath = chainPath.withRack(nestedRackId);
        auto* nestedRack = fixture.tm().getRackByPath(nestedPath);

        REQUIRE(nestedRack != nullptr);
        REQUIRE(nestedRack->id == nestedRackId);
        REQUIRE(nestedRack->name == "Nested Rack");
    }

    SECTION("Get deeply nested rack by path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rack1 = fixture.tm().addRackToTrack(trackId, "Level 1");

        auto* r1 = fixture.tm().getRack(trackId, rack1);
        auto chain1 = r1->chains[0].id;

        // Level 2: nested rack
        auto path1 = ChainNodePath::chain(trackId, rack1, chain1);
        auto rack2 = fixture.tm().addRackToChainByPath(path1, "Level 2");

        // Get chain in nested rack
        auto* r2 = fixture.tm().getRackByPath(path1.withRack(rack2));
        REQUIRE(r2 != nullptr);
        auto chain2 = r2->chains[0].id;

        // Level 3: deeper nested rack
        auto path2 = path1.withRack(rack2).withChain(chain2);
        auto rack3 = fixture.tm().addRackToChainByPath(path2, "Level 3");

        // Verify we can find it
        auto* r3 = fixture.tm().getRackByPath(path2.withRack(rack3));
        REQUIRE(r3 != nullptr);
        REQUIRE(r3->id == rack3);
        REQUIRE(r3->name == "Level 3");
    }

    SECTION("Invalid path returns nullptr") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto path = ChainNodePath::rack(trackId, 9999);  // Non-existent rack

        auto* rack = fixture.tm().getRackByPath(path);
        REQUIRE(rack == nullptr);
    }
}

// ============================================================================
// Chain Operations Tests
// ============================================================================

TEST_CASE("TrackManager: Add Chain to Rack", "[trackmanager][chain][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Add chain to top-level rack") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto rackPath = ChainNodePath::rack(trackId, rackId);
        auto chainId = fixture.tm().addChainToRack(rackPath, "New Chain");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains.size() == 2);  // Default chain + new chain
        REQUIRE(chainId != INVALID_CHAIN_ID);

        bool found = false;
        for (const auto& chain : rack->chains) {
            if (chain.id == chainId) {
                REQUIRE(chain.name == "New Chain");
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }

    SECTION("Add chain to nested rack") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* topRack = fixture.tm().getRack(trackId, rackId);
        auto chainId = topRack->chains[0].id;

        // Add nested rack
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);
        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");

        // Add chain to nested rack
        auto nestedRackPath = chainPath.withRack(nestedRackId);
        auto newChainId = fixture.tm().addChainToRack(nestedRackPath, "Nested Chain");

        // Verify chain was added
        auto* nestedRack = fixture.tm().getRackByPath(nestedRackPath);
        REQUIRE(nestedRack != nullptr);
        REQUIRE(nestedRack->chains.size() == 2);  // Default + new
        REQUIRE(newChainId != INVALID_CHAIN_ID);
    }
}

TEST_CASE("TrackManager: Remove Chain by Path", "[trackmanager][chain][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Remove chain from top-level rack") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        // Add extra chain
        auto rackPath = ChainNodePath::rack(trackId, rackId);
        auto chainId = fixture.tm().addChainToRack(rackPath, "Extra Chain");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains.size() == 2);

        // Remove the extra chain
        auto chainPath = rackPath.withChain(chainId);
        fixture.tm().removeChainByPath(chainPath);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains.size() == 1);
    }

    SECTION("Remove chain from nested rack") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* topRack = fixture.tm().getRack(trackId, rackId);
        auto chainId = topRack->chains[0].id;

        // Add nested rack with extra chain
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);
        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");
        auto nestedRackPath = chainPath.withRack(nestedRackId);
        auto extraChainId = fixture.tm().addChainToRack(nestedRackPath, "Extra");

        auto* nestedRack = fixture.tm().getRackByPath(nestedRackPath);
        REQUIRE(nestedRack->chains.size() == 2);

        // Remove extra chain
        auto extraChainPath = nestedRackPath.withChain(extraChainId);
        fixture.tm().removeChainByPath(extraChainPath);

        nestedRack = fixture.tm().getRackByPath(nestedRackPath);
        REQUIRE(nestedRack->chains.size() == 1);
    }
}

// ============================================================================
// Device Operations Tests
// ============================================================================

TEST_CASE("TrackManager: Add Device to Chain by Path", "[trackmanager][device][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Add device to top-level chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;

        DeviceInfo device;
        device.name = "Test Device";

        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);
        auto deviceId = fixture.tm().addDeviceToChainByPath(chainPath, device);

        REQUIRE(deviceId != INVALID_DEVICE_ID);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 1);
        REQUIRE(isDevice(rack->chains[0].elements[0]));
        REQUIRE(getDevice(rack->chains[0].elements[0]).name == "Test Device");
    }

    SECTION("Add multiple devices to chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo dev1, dev2, dev3;
        dev1.name = "Device 1";
        dev2.name = "Device 2";
        dev3.name = "Device 3";

        fixture.tm().addDeviceToChainByPath(chainPath, dev1);
        fixture.tm().addDeviceToChainByPath(chainPath, dev2);
        fixture.tm().addDeviceToChainByPath(chainPath, dev3);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 3);
    }

    SECTION("Add device to deeply nested chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rack1 = fixture.tm().addRackToTrack(trackId, "Level 1");

        auto* r1 = fixture.tm().getRack(trackId, rack1);
        auto chain1 = r1->chains[0].id;

        // Nested rack
        auto path1 = ChainNodePath::chain(trackId, rack1, chain1);
        auto rack2 = fixture.tm().addRackToChainByPath(path1, "Level 2");

        auto* r2 = fixture.tm().getRackByPath(path1.withRack(rack2));
        auto chain2 = r2->chains[0].id;

        // Add device to nested chain
        auto deepChainPath = path1.withRack(rack2).withChain(chain2);
        DeviceInfo device;
        device.name = "Deep Device";
        auto deviceId = fixture.tm().addDeviceToChainByPath(deepChainPath, device);

        REQUIRE(deviceId != INVALID_DEVICE_ID);

        // Verify device exists
        r2 = fixture.tm().getRackByPath(path1.withRack(rack2));
        REQUIRE(r2->chains[0].elements.size() == 1);
        REQUIRE(getDevice(r2->chains[0].elements[0]).name == "Deep Device");
    }
}

TEST_CASE("TrackManager: Remove Device by Path", "[trackmanager][device][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Remove device from chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo device;
        device.name = "Test Device";
        auto deviceId = fixture.tm().addDeviceToChainByPath(chainPath, device);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 1);

        // Remove device
        auto devicePath = chainPath.withDevice(deviceId);
        fixture.tm().removeDeviceFromChainByPath(devicePath);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.empty());
    }

    SECTION("Remove middle device from chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo dev1, dev2, dev3;
        dev1.name = "Device 1";
        dev2.name = "Device 2";
        dev3.name = "Device 3";

        fixture.tm().addDeviceToChainByPath(chainPath, dev1);
        auto device2Id = fixture.tm().addDeviceToChainByPath(chainPath, dev2);
        fixture.tm().addDeviceToChainByPath(chainPath, dev3);

        // Remove middle device
        fixture.tm().removeDeviceFromChainByPath(chainPath.withDevice(device2Id));

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 2);
        REQUIRE(getDevice(rack->chains[0].elements[0]).name == "Device 1");
        REQUIRE(getDevice(rack->chains[0].elements[1]).name == "Device 3");
    }
}

TEST_CASE("TrackManager: Get Device by Path", "[trackmanager][device][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Get existing device") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo device;
        device.name = "Test Device";
        device.gainDb = 3.0f;
        auto deviceId = fixture.tm().addDeviceToChainByPath(chainPath, device);

        auto* foundDevice = fixture.tm().getDeviceInChainByPath(chainPath.withDevice(deviceId));
        REQUIRE(foundDevice != nullptr);
        REQUIRE(foundDevice->name == "Test Device");
        REQUIRE(foundDevice->gainDb == 3.0f);
    }

    SECTION("Get non-existent device returns nullptr") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto devicePath = ChainNodePath::chain(trackId, rackId, chainId).withDevice(9999);

        auto* device = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(device == nullptr);
    }
}

TEST_CASE("TrackManager: Set Device Bypassed by Path", "[trackmanager][device][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Bypass device") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo device;
        device.name = "Test Device";
        device.bypassed = false;
        auto deviceId = fixture.tm().addDeviceToChainByPath(chainPath, device);

        auto devicePath = chainPath.withDevice(deviceId);

        // Bypass device
        fixture.tm().setDeviceInChainBypassedByPath(devicePath, true);

        auto* foundDevice = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(foundDevice != nullptr);
        REQUIRE(foundDevice->bypassed == true);

        // Unbypass device
        fixture.tm().setDeviceInChainBypassedByPath(devicePath, false);
        foundDevice = fixture.tm().getDeviceInChainByPath(devicePath);
        REQUIRE(foundDevice->bypassed == false);
    }
}

// ============================================================================
// Nested Rack Operations Tests
// ============================================================================

TEST_CASE("TrackManager: Add Nested Rack by Path", "[trackmanager][nested_rack][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Add nested rack to chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");
        REQUIRE(nestedRackId != INVALID_RACK_ID);

        // Verify nested rack exists
        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 1);
        REQUIRE(isRack(rack->chains[0].elements[0]));
        REQUIRE(getRack(rack->chains[0].elements[0]).name == "Nested Rack");
    }

    SECTION("Add deeply nested rack (level 3)") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rack1 = fixture.tm().addRackToTrack(trackId, "Level 1");

        auto* r1 = fixture.tm().getRack(trackId, rack1);
        auto chain1 = r1->chains[0].id;
        auto chainPath1 = ChainNodePath::chain(trackId, rack1, chain1);

        // Level 2
        auto rack2 = fixture.tm().addRackToChainByPath(chainPath1, "Level 2");
        auto* r2 = fixture.tm().getRackByPath(chainPath1.withRack(rack2));
        auto chain2 = r2->chains[0].id;
        auto chainPath2 = chainPath1.withRack(rack2).withChain(chain2);

        // Level 3
        auto rack3 = fixture.tm().addRackToChainByPath(chainPath2, "Level 3");
        auto* r3 = fixture.tm().getRackByPath(chainPath2.withRack(rack3));
        auto chain3 = r3->chains[0].id;
        auto chainPath3 = chainPath2.withRack(rack3).withChain(chain3);

        // Level 4
        auto rack4 = fixture.tm().addRackToChainByPath(chainPath3, "Level 4");
        REQUIRE(rack4 != INVALID_RACK_ID);

        // Verify level 4 rack exists
        auto* r4 = fixture.tm().getRackByPath(chainPath3.withRack(rack4));
        REQUIRE(r4 != nullptr);
        REQUIRE(r4->name == "Level 4");
    }

    SECTION("Nested rack has default chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");

        auto* nestedRack = fixture.tm().getRackByPath(chainPath.withRack(nestedRackId));
        REQUIRE(nestedRack != nullptr);
        REQUIRE(nestedRack->chains.size() == 1);
        REQUIRE(nestedRack->chains[0].name == "Chain 1");
    }
}

TEST_CASE("TrackManager: Remove Nested Rack by Path", "[trackmanager][nested_rack][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Remove nested rack from chain") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");

        // Verify it exists
        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.size() == 1);

        // Remove it
        fixture.tm().removeRackFromChainByPath(chainPath.withRack(nestedRackId));

        // Verify it's gone
        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->chains[0].elements.empty());
    }

    SECTION("Remove deeply nested rack") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rack1 = fixture.tm().addRackToTrack(trackId, "Level 1");

        auto* r1 = fixture.tm().getRack(trackId, rack1);
        auto chain1 = r1->chains[0].id;
        auto chainPath1 = ChainNodePath::chain(trackId, rack1, chain1);

        // Level 2
        auto rack2 = fixture.tm().addRackToChainByPath(chainPath1, "Level 2");
        auto* r2 = fixture.tm().getRackByPath(chainPath1.withRack(rack2));
        auto chain2 = r2->chains[0].id;
        auto chainPath2 = chainPath1.withRack(rack2).withChain(chain2);

        // Level 3
        auto rack3 = fixture.tm().addRackToChainByPath(chainPath2, "Level 3");
        REQUIRE(rack3 != INVALID_RACK_ID);

        // Verify level 3 exists
        r2 = fixture.tm().getRackByPath(chainPath1.withRack(rack2));
        REQUIRE(r2->chains[0].elements.size() == 1);

        // Remove level 3
        fixture.tm().removeRackFromChainByPath(chainPath2.withRack(rack3));

        // Verify it's gone
        r2 = fixture.tm().getRackByPath(chainPath1.withRack(rack2));
        REQUIRE(r2->chains[0].elements.empty());
    }
}

// ============================================================================
// Path Resolution Tests
// ============================================================================

TEST_CASE("TrackManager: resolvePath", "[trackmanager][path][resolution]") {
    TrackManagerTestFixture fixture;

    SECTION("Resolve rack path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto path = ChainNodePath::rack(trackId, rackId);
        auto resolved = fixture.tm().resolvePath(path);

        REQUIRE(resolved.valid);
        REQUIRE(resolved.rack != nullptr);
        REQUIRE(resolved.rack->name == "Test Rack");
        REQUIRE(resolved.chain == nullptr);
        REQUIRE(resolved.device == nullptr);
    }

    SECTION("Resolve chain path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;

        auto path = ChainNodePath::chain(trackId, rackId, chainId);
        auto resolved = fixture.tm().resolvePath(path);

        REQUIRE(resolved.valid);
        REQUIRE(resolved.rack != nullptr);
        REQUIRE(resolved.chain != nullptr);
        REQUIRE(resolved.chain->name == "Chain 1");
        REQUIRE(resolved.device == nullptr);
    }

    SECTION("Resolve device path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo device;
        device.name = "Test Device";
        auto deviceId = fixture.tm().addDeviceToChainByPath(chainPath, device);

        auto path = chainPath.withDevice(deviceId);
        auto resolved = fixture.tm().resolvePath(path);

        REQUIRE(resolved.valid);
        REQUIRE(resolved.rack != nullptr);
        REQUIRE(resolved.chain != nullptr);
        REQUIRE(resolved.device != nullptr);
        REQUIRE(resolved.device->name == "Test Device");
    }

    SECTION("Resolve nested rack path") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Top Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        auto nestedRackId = fixture.tm().addRackToChainByPath(chainPath, "Nested Rack");

        auto path = chainPath.withRack(nestedRackId);
        auto resolved = fixture.tm().resolvePath(path);

        REQUIRE(resolved.valid);
        REQUIRE(resolved.rack != nullptr);
        REQUIRE(resolved.rack->name == "Nested Rack");
    }

    SECTION("Invalid path returns invalid result") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto path = ChainNodePath::rack(trackId, 9999);  // Non-existent rack

        auto resolved = fixture.tm().resolvePath(path);
        REQUIRE_FALSE(resolved.valid);
    }

    SECTION("Display path contains names") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "My Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        DeviceInfo device;
        device.name = "Compressor";
        auto deviceId = fixture.tm().addDeviceToChainByPath(chainPath, device);

        auto path = chainPath.withDevice(deviceId);
        auto resolved = fixture.tm().resolvePath(path);

        REQUIRE(resolved.valid);
        REQUIRE(resolved.displayPath.contains("My Rack"));
        REQUIRE(resolved.displayPath.contains("Chain 1"));
        REQUIRE(resolved.displayPath.contains("Compressor"));
    }
}

// ============================================================================
// Mixed Operations Tests
// ============================================================================

TEST_CASE("TrackManager: Mixed Devices and Racks in Chain", "[trackmanager][mixed][path]") {
    TrackManagerTestFixture fixture;

    SECTION("Chain with devices and nested rack") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        auto chainId = rack->chains[0].id;
        auto chainPath = ChainNodePath::chain(trackId, rackId, chainId);

        // Add device, then rack, then device
        DeviceInfo dev1, dev2;
        dev1.name = "EQ";
        dev2.name = "Limiter";

        fixture.tm().addDeviceToChainByPath(chainPath, dev1);
        fixture.tm().addRackToChainByPath(chainPath, "Parallel Rack");
        fixture.tm().addDeviceToChainByPath(chainPath, dev2);

        rack = fixture.tm().getRack(trackId, rackId);
        auto& elements = rack->chains[0].elements;

        REQUIRE(elements.size() == 3);
        REQUIRE(isDevice(elements[0]));
        REQUIRE(getDevice(elements[0]).name == "EQ");
        REQUIRE(isRack(elements[1]));
        REQUIRE(getRack(elements[1]).name == "Parallel Rack");
        REQUIRE(isDevice(elements[2]));
        REQUIRE(getDevice(elements[2]).name == "Limiter");
    }
}

TEST_CASE("TrackManager: Rack Bypass Operations", "[trackmanager][rack][bypass]") {
    TrackManagerTestFixture fixture;

    SECTION("Set rack bypassed") {
        auto trackId = fixture.tm().createTrack("Test Track");
        auto rackId = fixture.tm().addRackToTrack(trackId, "Test Rack");

        auto* rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE_FALSE(rack->bypassed);

        fixture.tm().setRackBypassed(trackId, rackId, true);

        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE(rack->bypassed);

        fixture.tm().setRackBypassed(trackId, rackId, false);
        rack = fixture.tm().getRack(trackId, rackId);
        REQUIRE_FALSE(rack->bypassed);
    }
}
