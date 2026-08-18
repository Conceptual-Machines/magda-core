#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "magda/daw/core/AutomationInfo.hpp"
#include "magda/daw/core/DeviceParamMigrations.hpp"
#include "magda/daw/core/TrackInfo.hpp"

// -----------------------------------------------------------------------------
// paramIndex migrations (#2079)
//
// The mechanics are driven with a table of this file's own, so the behaviour is
// pinned independently of which devices happen to need a migration today. The
// shipped entries are checked here too, for the shape a saved file has to have
// for them to fire; where their values and links actually land in a real
// project is asserted against the corpus in test_legacy_corpus.cpp.
// -----------------------------------------------------------------------------

namespace magda {
namespace {

namespace migrations = device_param_migrations;

constexpr const char* kDeviceType = "magda_test_device";

/// Swap the first two parameters, retire the third, and add one at index 3 that
/// has to be switched on for the device to behave as it did. The new order is 4
/// parameters long against the old order's 4 - so the last one is dropped too,
/// keeping the counts different and the migration a one-shot.
constexpr std::array<int, 4> kSwapFirstTwo = {1, 0, migrations::kDropped, migrations::kDropped};
constexpr std::array<migrations::SeededParam, 1> kSeeded = {
    migrations::SeededParam{2, "Enabled", 1.0f}};

migrations::MigrationTable testTable() {
    migrations::MigrationTable table;
    migrations::ParamIndexMigration entry;
    entry.deviceType = kDeviceType;
    entry.reason = "swap the first two parameters, retire the third, add an enable switch";
    entry.savedParamCount = static_cast<int>(kSwapFirstTwo.size());
    entry.oldToNew = kSwapFirstTwo;
    entry.seeded = kSeeded;
    table.push_back(entry);
    return table;
}

DeviceInfo makeDevice(DeviceId id, const juce::String& pluginId = kDeviceType) {
    DeviceInfo device;
    device.id = id;
    device.pluginId = pluginId;
    device.name = "Test Device";
    for (int index = 0; index < 4; ++index) {
        ParameterInfo param;
        param.paramIndex = index;
        param.name = "Param " + juce::String(index);
        param.currentValue = static_cast<float>(index);
        device.parameters.push_back(param);
    }
    device.visibleParameters = {0, 1, 2, 3};
    return device;
}

TrackInfo makeTrackWithDevice(TrackId trackId, DeviceId deviceId) {
    TrackInfo track;
    track.id = trackId;
    track.name = "Track";
    track.chain.fxChainElements.push_back(makeDevice(deviceId));
    // A track starts with no modulators (createDefaultMods(0)); a project that
    // has one wrote it, so the test has to add one before linking through it.
    track.mods.push_back(ModInfo(0));
    return track;
}

ControlTarget targetFor(TrackId trackId, DeviceId deviceId, int paramIndex) {
    return ControlTarget::pluginParam(ChainNodePath::topLevelDevice(trackId, deviceId), paramIndex);
}

}  // namespace

TEST_CASE("A migration matches on the device type and the saved parameter count",
          "[migration][params]") {
    const auto table = testTable();

    auto device = makeDevice(1);
    REQUIRE(migrations::findMigration(device, table) != nullptr);

    // Same device, a different order: not this migration's file.
    device.parameters.pop_back();
    CHECK(migrations::findMigration(device, table) == nullptr);

    // Another device entirely.
    auto other = makeDevice(2, "magda_reverb");
    CHECK(migrations::findMigration(other, table) == nullptr);
}

TEST_CASE("A migrated index moves to where the parameter went", "[migration][params]") {
    const auto table = testTable();
    const auto& migration = table.front();

    CHECK(migrations::migratedParamIndex(migration, 0) == 1);
    CHECK(migrations::migratedParamIndex(migration, 1) == 0);

    // Retired parameters: dropped rather than moved onto a neighbour.
    CHECK(!migrations::migratedParamIndex(migration, 2).has_value());
    CHECK(!migrations::migratedParamIndex(migration, 3).has_value());

    // Past the end of the order the migration describes: the file and the
    // migration disagree, so the link goes rather than landing anywhere.
    CHECK(!migrations::migratedParamIndex(migration, 4).has_value());
}

TEST_CASE("A device's own saved parameters follow the migration", "[migration][params]") {
    const auto table = testTable();

    auto device = makeDevice(11);
    migrations::migrateDevicePreset(device, table);

    REQUIRE(device.parameters.size() == 3);
    // Sorted by the new index, values carried with their parameter.
    CHECK(device.parameters[0].name == "Param 1");
    CHECK(device.parameters[0].paramIndex == 0);
    CHECK(device.parameters[0].currentValue == 1.0f);
    CHECK(device.parameters[1].name == "Param 0");
    CHECK(device.parameters[1].paramIndex == 1);

    // The parameter the new order added, seeded rather than left at a default
    // that would change what the device does.
    CHECK(device.parameters[2].name == "Enabled");
    CHECK(device.parameters[2].paramIndex == 2);
    CHECK(device.parameters[2].currentValue == 1.0f);

    // The panel selections address parameters too.
    REQUIRE(device.visibleParameters.size() == 2);
    CHECK(device.visibleParameters[0] == 1);
    CHECK(device.visibleParameters[1] == 0);

    // Migrating again is a no-op: the count no longer matches the old order.
    migrations::migrateDevicePreset(device, table);
    CHECK(device.parameters.size() == 3);
    CHECK(device.parameters[0].name == "Param 1");
}

TEST_CASE("Saved links follow the migration onto their parameter", "[migration][params]") {
    const auto table = testTable();

    constexpr TrackId kTrackId = 1;
    constexpr DeviceId kDeviceId = 42;

    std::vector<TrackInfo> tracks;
    tracks.push_back(makeTrackWithDevice(kTrackId, kDeviceId));

    auto& track = tracks.front();
    auto& device = getDevice(track.chain.fxChainElements.front());

    MacroLink macroLink;
    macroLink.target = targetFor(kTrackId, kDeviceId, 0);
    macroLink.amount = 1.0f;
    device.macros[0].links.push_back(macroLink);

    ModLink modLink;
    modLink.target = targetFor(kTrackId, kDeviceId, 1);
    modLink.amount = 0.5f;
    track.mods[0].links.push_back(modLink);

    // A link onto the parameter the migration retires.
    MacroLink doomed;
    doomed.target = targetFor(kTrackId, kDeviceId, 2);
    track.macros[0].links.push_back(doomed);

    std::vector<AutomationLaneInfo> lanes;
    AutomationLaneInfo kept;
    kept.id = 1;
    kept.target = targetFor(kTrackId, kDeviceId, 0);
    lanes.push_back(kept);

    AutomationLaneInfo dropped;
    dropped.id = 2;
    dropped.target = targetFor(kTrackId, kDeviceId, 2);
    lanes.push_back(dropped);

    std::vector<AutomationClipInfo> automationClips;
    AutomationClipInfo clipOnKeptLane;
    clipOnKeptLane.id = 1;
    clipOnKeptLane.laneId = 1;
    automationClips.push_back(clipOnKeptLane);

    AutomationClipInfo clipOnDroppedLane;
    clipOnDroppedLane.id = 2;
    clipOnDroppedLane.laneId = 2;
    automationClips.push_back(clipOnDroppedLane);

    migrations::applyParamIndexMigrations(tracks, nullptr, lanes, automationClips, table);

    auto& migratedTrack = tracks.front();
    auto& migratedDevice = getDevice(migratedTrack.chain.fxChainElements.front());

    REQUIRE(migratedDevice.macros[0].links.size() == 1);
    CHECK(migratedDevice.macros[0].links[0].target.paramIndex == 1);

    REQUIRE(migratedTrack.mods[0].links.size() == 1);
    CHECK(migratedTrack.mods[0].links[0].target.paramIndex == 0);

    // The link onto the retired parameter is gone, not re-pointed.
    CHECK(migratedTrack.macros[0].links.empty());

    REQUIRE(lanes.size() == 1);
    CHECK(lanes[0].id == 1);
    CHECK(lanes[0].target.paramIndex == 1);

    // The dropped lane takes its clips with it.
    REQUIRE(automationClips.size() == 1);
    CHECK(automationClips[0].laneId == 1);
}

TEST_CASE("A link into a device the migration does not name is untouched", "[migration][params]") {
    const auto table = testTable();

    constexpr TrackId kTrackId = 1;
    constexpr DeviceId kDeviceId = 7;

    std::vector<TrackInfo> tracks;
    TrackInfo track;
    track.id = kTrackId;
    track.chain.fxChainElements.push_back(makeDevice(kDeviceId, "magda_reverb"));

    MacroLink link;
    link.target = targetFor(kTrackId, kDeviceId, 2);
    track.macros[0].links.push_back(link);
    tracks.push_back(std::move(track));

    std::vector<AutomationLaneInfo> lanes;
    std::vector<AutomationClipInfo> automationClips;
    migrations::applyParamIndexMigrations(tracks, nullptr, lanes, automationClips, table);

    REQUIRE(tracks.front().macros[0].links.size() == 1);
    CHECK(tracks.front().macros[0].links[0].target.paramIndex == 2);
    CHECK(getDevice(tracks.front().chain.fxChainElements.front()).parameters.size() == 4);
}

TEST_CASE("Devices inside a rack and on the master track migrate too", "[migration][params]") {
    const auto table = testTable();

    constexpr TrackId kTrackId = 3;
    constexpr DeviceId kRackDeviceId = 21;
    constexpr DeviceId kMasterDeviceId = 22;

    std::vector<TrackInfo> tracks;
    TrackInfo track;
    track.id = kTrackId;

    auto rack = std::make_unique<RackInfo>();
    rack->id = 5;
    ChainInfo chain;
    chain.id = 6;
    chain.elements.push_back(makeDevice(kRackDeviceId));
    rack->chains.push_back(std::move(chain));

    MacroLink rackLink;
    rackLink.target = ControlTarget::pluginParam(
        ChainNodePath::rack(kTrackId, rack->id).withChain(6).withDevice(kRackDeviceId), 0);
    rack->macros[0].links.push_back(rackLink);

    track.chain.fxChainElements.push_back(std::move(rack));
    tracks.push_back(std::move(track));

    TrackInfo master;
    master.id = MASTER_TRACK_ID;
    master.chain.fxChainElements.push_back(makeDevice(kMasterDeviceId));

    std::vector<AutomationLaneInfo> lanes;
    AutomationLaneInfo lane;
    lane.id = 1;
    lane.target = targetFor(MASTER_TRACK_ID, kMasterDeviceId, 1);
    lanes.push_back(lane);

    std::vector<AutomationClipInfo> automationClips;
    migrations::applyParamIndexMigrations(tracks, &master, lanes, automationClips, table);

    auto& migratedRack = getRack(tracks.front().chain.fxChainElements.front());
    REQUIRE(migratedRack.macros[0].links.size() == 1);
    CHECK(migratedRack.macros[0].links[0].target.paramIndex == 1);
    CHECK(getDevice(migratedRack.chains[0].elements[0]).parameters.size() == 3);

    CHECK(getDevice(master.chain.fxChainElements.front()).parameters.size() == 3);
    REQUIRE(lanes.size() == 1);
    CHECK(lanes[0].target.paramIndex == 0);
}

TEST_CASE("The shipped migrations describe the orders they claim to", "[migration][params]") {
    const auto& shipped = migrations::shippedMigrations();

    for (const auto& migration : shipped) {
        INFO("migration for '" << migration.deviceType << "': " << migration.reason);
        CHECK(migration.savedParamCount > 0);
        CHECK(static_cast<int>(migration.oldToNew.size()) == migration.savedParamCount);
        CHECK(juce::String(migration.reason).isNotEmpty());

        // No two old indices may land on the same new one: that would be two
        // saved values fighting over one parameter.
        std::vector<int> targets;
        for (int index : migration.oldToNew)
            if (index != migrations::kDropped)
                targets.push_back(index);
        auto sorted = targets;
        std::sort(sorted.begin(), sorted.end());
        CHECK(std::unique(sorted.begin(), sorted.end()) == sorted.end());

        // A seeded parameter must not collide with a migrated one either.
        for (const auto& seed : migration.seeded) {
            INFO("seeded '" << seed.name << "' at " << seed.index);
            CHECK(std::find(targets.begin(), targets.end(), seed.index) == targets.end());
        }

        // The order a migrated device ends up in must not look like the order
        // that triggers the migration, or every load would migrate it again and
        // permute the parameters one more time.
        const auto migratedCount = static_cast<int>(targets.size() + migration.seeded.size());
        INFO("a device migrated by this entry has " << migratedCount
                                                    << " parameters, which is what the entry "
                                                       "matches on - it would fire forever");
        CHECK(migratedCount != migration.savedParamCount);
    }
}

TEST_CASE("The EQ migration moves every band down one slot", "[migration][params]") {
    const auto& shipped = migrations::shippedMigrations();
    const auto eq = std::find_if(shipped.begin(), shipped.end(), [](const auto& migration) {
        return juce::String(migration.deviceType) == "magda_eq";
    });
    REQUIRE(eq != shipped.end());
    REQUIRE(eq->savedParamCount == 33);

    // Band 1: type, freq, gain, Q at 0-3 become 1-4, behind the new Enabled at 0.
    CHECK(migrations::migratedParamIndex(*eq, 0) == 1);
    CHECK(migrations::migratedParamIndex(*eq, 3) == 4);
    // Band 2 starts at 5 rather than 4.
    CHECK(migrations::migratedParamIndex(*eq, 4) == 6);
    // Band 8's Q, then the output trim at the end.
    CHECK(migrations::migratedParamIndex(*eq, 31) == 39);
    CHECK(migrations::migratedParamIndex(*eq, 32) == 40);

    // All eight bands are switched on: they were always running before the
    // Enabled switch existed, and it defaults to off.
    REQUIRE(eq->seeded.size() == 8);
    for (size_t band = 0; band < eq->seeded.size(); ++band) {
        CHECK(eq->seeded[band].index == static_cast<int>(band) * 5);
        CHECK(eq->seeded[band].value == 1.0f);
    }
}

TEST_CASE("The limiter migration drops the three parameters it lost", "[migration][params]") {
    const auto& shipped = migrations::shippedMigrations();
    const auto limiter = std::find_if(shipped.begin(), shipped.end(), [](const auto& migration) {
        return juce::String(migration.deviceType) == "magda_limiter";
    });
    REQUIRE(limiter != shipped.end());
    REQUIRE(limiter->savedParamCount == 7);

    CHECK(migrations::migratedParamIndex(*limiter, 0) == 0);  // Threshold
    CHECK(migrations::migratedParamIndex(*limiter, 1) == 1);  // Attack
    CHECK(!migrations::migratedParamIndex(*limiter, 2));      // Hold, gone
    CHECK(migrations::migratedParamIndex(*limiter, 3) == 2);  // Release
    CHECK(!migrations::migratedParamIndex(*limiter, 4));      // Mix, gone
    CHECK(migrations::migratedParamIndex(*limiter, 5) == 3);  // Output
    CHECK(!migrations::migratedParamIndex(*limiter, 6));      // Autogain, gone
    CHECK(limiter->seeded.empty());
}

}  // namespace magda
