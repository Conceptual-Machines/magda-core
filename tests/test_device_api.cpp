#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <set>

#include "magda/daw/api/device_api_live.hpp"
#include "magda/daw/core/TrackCommands.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

using namespace magda;

namespace {

/// A catalog id that exists in every build, so add tests need no plugin scan.
juce::String anyCatalogId() {
    const DeviceApiLive devices;
    const auto catalog = devices.getCatalog();
    REQUIRE_FALSE(catalog.empty());
    return catalog.front().catalogId;
}

TrackId freshTrack(const juce::String& name) {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    UndoManager::getInstance().clearHistory();
    return tracks.createTrack(name, TrackType::Audio);
}

}  // namespace

TEST_CASE("The device catalog is discoverable and internally consistent", "[device-api][catalog]") {
    const DeviceApiLive devices;
    const auto catalog = devices.getCatalog();

    // The compiled and internal registries are linked into this binary, so the
    // catalog is never empty even with no plugin scan.
    REQUIRE_FALSE(catalog.empty());

    std::set<juce::String> ids;
    for (const auto& entry : catalog) {
        INFO("catalog entry: " << entry.name);
        // A caller addresses a device by this and nothing else.
        REQUIRE(entry.catalogId.isNotEmpty());
        // Ids must be unique or addDevice would be ambiguous.
        REQUIRE(ids.insert(entry.catalogId).second);
        REQUIRE(entry.isInstrument == (entry.type == DeviceType::Instrument));
    }
}

TEST_CASE("Every catalog entry resolves by its own id", "[device-api][catalog]") {
    const DeviceApiLive devices;
    for (const auto& entry : devices.getCatalog()) {
        const auto found = devices.findCatalogEntry(entry.catalogId);
        REQUIRE(found.has_value());
        REQUIRE(*found == entry);
    }
}

TEST_CASE("Unknown catalog ids resolve to nothing", "[device-api][catalog]") {
    const DeviceApiLive devices;
    REQUIRE_FALSE(devices.findCatalogEntry("not_a_device").has_value());
    REQUIRE_FALSE(devices.findCatalogEntry("").has_value());
}

TEST_CASE("Live device lookup rejects paths that address nothing", "[device-api][inspection]") {
    const DeviceApiLive devices;

    // A default path names no node at all.
    REQUIRE(devices.getDevice(ChainNodePath{}) == nullptr);
    REQUIRE(devices.getDeviceParameters(ChainNodePath{}).empty());

    // A well-formed path to a track that does not exist.
    const auto missing = ChainNodePath::topLevelDevice(9999, 1);
    REQUIRE(devices.getDevice(missing) == nullptr);
    REQUIRE(devices.getDeviceParameters(missing).empty());
}

TEST_CASE("Device parameters are reported in real units", "[device-api][inspection]") {
    auto& tracks = TrackManager::getInstance();
    tracks.clearAllTracks();
    const auto trackId = tracks.createTrack("Synth", TrackType::Audio);

    DeviceInfo device;
    device.name = "Filter";
    device.pluginId = "test_filter";
    ParameterInfo cutoff;
    cutoff.paramIndex = 0;
    cutoff.stableId = "cutoff";
    cutoff.name = "Cutoff";
    cutoff.unit = "Hz";
    cutoff.minValue = 20.0f;
    cutoff.maxValue = 20000.0f;
    cutoff.defaultValue = 1000.0f;
    cutoff.currentValue = 440.0f;
    device.parameters.push_back(cutoff);

    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    const DeviceApiLive devices;
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(path.isValid());
    REQUIRE(devices.getDevice(path) != nullptr);

    const auto parameters = devices.getDeviceParameters(path);
    REQUIRE(parameters.size() == 1);
    // Real parameter units, not MAGDA-normalized automation units.
    REQUIRE(parameters[0].name == "Cutoff");
    REQUIRE(parameters[0].unit == "Hz");
    REQUIRE(parameters[0].minValue == 20.0f);
    REQUIRE(parameters[0].maxValue == 20000.0f);
    REQUIRE(parameters[0].currentValue == 440.0f);
    REQUIRE(parameters[0].stableId == "cutoff");

    tracks.clearAllTracks();
}

// ============================================================================
// Mutations
// ============================================================================

TEST_CASE("Adding a device by catalog id is one undo step", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    const auto catalogId = anyCatalogId();

    DeviceApiLive devices;
    const auto deviceId =
        devices.addDevice(ChainNodePath::trackLevel(trackId), catalogId, /*index=*/-1);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto& tracks = TrackManager::getInstance();
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(path.isValid());
    const auto* added = devices.getDevice(path);
    REQUIRE(added != nullptr);
    REQUIRE(added->pluginId == catalogId);

    // One Undo removes it, and one Redo brings it back.
    auto& undo = UndoManager::getInstance();
    REQUIRE(undo.canUndo());
    undo.undo();
    REQUIRE(tracks.getChainElements(trackId).empty());

    undo.redo();
    REQUIRE(tracks.getChainElements(trackId).size() == 1);

    tracks.clearAllTracks();
}

TEST_CASE("Adding a device rejects placements that do not resolve", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    const auto catalogId = anyCatalogId();
    DeviceApiLive devices;

    SECTION("unknown catalog id") {
        REQUIRE(devices.addDevice(ChainNodePath::trackLevel(trackId), "not_a_device", -1) ==
                INVALID_DEVICE_ID);
    }

    SECTION("track that does not exist") {
        REQUIRE(devices.addDevice(ChainNodePath::trackLevel(9999), catalogId, -1) ==
                INVALID_DEVICE_ID);
    }

    SECTION("a device path is not a placement") {
        // Only a track or a chain can hold a device.
        REQUIRE(devices.addDevice(ChainNodePath::topLevelDevice(trackId, 1), catalogId, -1) ==
                INVALID_DEVICE_ID);
    }

    SECTION("chain that does not exist") {
        REQUIRE(devices.addDevice(ChainNodePath::chain(trackId, 42, 43), catalogId, -1) ==
                INVALID_DEVICE_ID);
    }

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Removing a device restores it on undo", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;
    const auto deviceId = devices.addDevice(ChainNodePath::trackLevel(trackId), anyCatalogId(), -1);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    auto& tracks = TrackManager::getInstance();
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(devices.removeDevice(path));
    REQUIRE(tracks.getChainElements(trackId).empty());

    UndoManager::getInstance().undo();
    REQUIRE(tracks.getChainElements(trackId).size() == 1);

    // Removing something that is not there is a failure, not a silent no-op.
    REQUIRE_FALSE(devices.removeDevice(ChainNodePath::topLevelDevice(trackId, 9999)));
    REQUIRE_FALSE(devices.removeDevice(ChainNodePath{}));

    tracks.clearAllTracks();
}

TEST_CASE("Undoing a removal restores the device under its original id",
          "[device-api][mutation][undo]") {
    // The ordinary add path stamps a fresh DeviceId via prepareNewDevice. If
    // undo went through it, the device would come back under a different id and
    // every automation lane, macro link, and alias targeting it would dangle.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;

    const auto deviceId = devices.addDevice(ChainNodePath::trackLevel(trackId), anyCatalogId(), -1);
    const auto path = tracks.findDevicePath(deviceId);
    REQUIRE(path.isValid());

    REQUIRE(devices.removeDevice(path));
    UndoManager::getInstance().undo();

    // Same id, and therefore still reachable at the path that addressed it.
    REQUIRE(tracks.findDevicePath(deviceId) == path);
    REQUIRE(devices.getDevice(path) != nullptr);
    REQUIRE(devices.getDevice(path)->id == deviceId);

    tracks.clearAllTracks();
}

TEST_CASE("The track-level remove command also preserves the device id",
          "[device-api][mutation][undo]") {
    // RemoveDeviceFromTrackCommand is what the UI runs when a device is deleted
    // from a track, and it had the same id-reassigning undo.
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Bass");

    DeviceInfo device;
    device.name = "Filter";
    device.pluginId = "test_filter";
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    REQUIRE(deviceId != INVALID_DEVICE_ID);
    const auto path = tracks.findDevicePath(deviceId);

    auto& undo = UndoManager::getInstance();
    undo.executeCommand(std::make_unique<RemoveDeviceFromTrackCommand>(trackId, deviceId));
    REQUIRE(tracks.getChainElements(trackId).empty());

    undo.undo();
    REQUIRE(tracks.getChainElements(trackId).size() == 1);
    REQUIRE(tracks.findDevicePath(deviceId) == path);

    tracks.clearAllTracks();
}

TEST_CASE("Undo restores a device to its original position", "[device-api][mutation][undo]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;
    const auto catalogId = anyCatalogId();
    const auto trackPath = ChainNodePath::trackLevel(trackId);

    const auto first = devices.addDevice(trackPath, catalogId, -1);
    const auto middle = devices.addDevice(trackPath, catalogId, -1);
    const auto last = devices.addDevice(trackPath, catalogId, -1);
    REQUIRE(tracks.getChainElements(trackId).size() == 3);

    REQUIRE(devices.removeDevice(tracks.findDevicePath(middle)));
    REQUIRE(tracks.getChainElements(trackId).size() == 2);

    UndoManager::getInstance().undo();
    const auto& elements = tracks.getChainElements(trackId);
    REQUIRE(elements.size() == 3);
    // Back in the middle, not appended to the end.
    REQUIRE(getDevice(elements[0]).id == first);
    REQUIRE(getDevice(elements[1]).id == middle);
    REQUIRE(getDevice(elements[2]).id == last);

    tracks.clearAllTracks();
}

TEST_CASE("Bypass is applied and reported through the facade", "[device-api][mutation]") {
    const auto trackId = freshTrack("Bass");
    DeviceApiLive devices;
    const auto deviceId = devices.addDevice(ChainNodePath::trackLevel(trackId), anyCatalogId(), -1);
    const auto path = TrackManager::getInstance().findDevicePath(deviceId);

    REQUIRE(devices.setDeviceBypassed(path, true));
    REQUIRE(devices.getDevice(path)->bypassed);
    REQUIRE(devices.setDeviceBypassed(path, false));
    REQUIRE_FALSE(devices.getDevice(path)->bypassed);

    REQUIRE_FALSE(devices.setDeviceBypassed(ChainNodePath{}, true));

    TrackManager::getInstance().clearAllTracks();
}

TEST_CASE("Parameter writes are range-checked rather than clamped", "[device-api][mutation]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Synth");

    DeviceInfo device;
    device.name = "Filter";
    device.pluginId = "test_filter";
    ParameterInfo cutoff;
    cutoff.paramIndex = 0;
    cutoff.name = "Cutoff";
    cutoff.minValue = 20.0f;
    cutoff.maxValue = 20000.0f;
    cutoff.currentValue = 440.0f;
    device.parameters.push_back(cutoff);
    const auto deviceId = tracks.addDeviceToTrack(trackId, device);
    const auto path = tracks.findDevicePath(deviceId);

    DeviceApiLive devices;
    REQUIRE(devices.setDeviceParameter(path, 0, 880.0f));

    SECTION("out of range is refused") {
        // Clamping would report success while setting something else.
        REQUIRE_FALSE(devices.setDeviceParameter(path, 0, 19.0f));
        REQUIRE_FALSE(devices.setDeviceParameter(path, 0, 20001.0f));
        REQUIRE_FALSE(devices.setDeviceParameter(path, 0, std::numeric_limits<float>::quiet_NaN()));
    }

    SECTION("unknown parameter index is refused") {
        REQUIRE_FALSE(devices.setDeviceParameter(path, 7, 100.0f));
    }

    SECTION("unknown device is refused") {
        REQUIRE_FALSE(devices.setDeviceParameter(ChainNodePath{}, 0, 100.0f));
    }

    tracks.clearAllTracks();
}

TEST_CASE("Devices can be added to a chain inside a rack", "[device-api][mutation]") {
    auto& tracks = TrackManager::getInstance();
    const auto trackId = freshTrack("Keys");

    const auto rackId = tracks.addRackToTrack(trackId, "Rack");
    REQUIRE(rackId != INVALID_RACK_ID);
    const auto rackPath = ChainNodePath::rack(trackId, rackId);
    const auto* rack = tracks.getRackByPath(rackPath);
    REQUIRE(rack != nullptr);
    REQUIRE_FALSE(rack->chains.empty());
    const auto chainPath = rackPath.withChain(rack->chains.front().id);

    DeviceApiLive devices;
    const auto deviceId = devices.addDevice(chainPath, anyCatalogId(), -1);
    REQUIRE(deviceId != INVALID_DEVICE_ID);

    // The device is reachable at the path that addresses it inside the chain.
    const auto devicePath = chainPath.withDevice(deviceId);
    REQUIRE(devices.getDevice(devicePath) != nullptr);

    REQUIRE(devices.removeDevice(devicePath));
    REQUIRE(devices.getDevice(devicePath) == nullptr);

    UndoManager::getInstance().undo();
    REQUIRE(devices.getDevice(devicePath) != nullptr);

    tracks.clearAllTracks();
}
