#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/api/device_api_live.hpp"
#include "magda/daw/core/TrackManager.hpp"

using namespace magda;

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
