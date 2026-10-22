#include <array>
#include <catch2/catch_approx.hpp>
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

TEST_CASE("A device id shared across sections does not migrate the wrong device",
          "[migration][params]") {
    const auto table = testTable();

    constexpr TrackId kTrackId = 4;
    // The main FX chain, the post-FX stage and the mixer-analysis rail allocate
    // device ids from their own counters, so the same number addresses three
    // different devices in one project. Only the FX-chain one is in the order
    // the migration describes.
    constexpr DeviceId kSharedId = 9;

    std::vector<TrackInfo> tracks;
    TrackInfo track;
    track.id = kTrackId;
    track.chain.fxChainElements.push_back(makeDevice(kSharedId));
    track.chain.postFxChainElements.push_back({makeDevice(kSharedId, "magda_reverb")});
    track.chain.mixerAnalysisElements.push_back({makeDevice(kSharedId, "oscilloscope")});

    // A macro on the post-FX device, which must not be touched.
    MacroLink postFxLink;
    postFxLink.target =
        ControlTarget::pluginParam(ChainNodePath::postFxDevice(kTrackId, kSharedId), 2);
    track.macros[0].links.push_back(postFxLink);

    // A macro on the FX-chain device with the same id, which must move.
    MacroLink fxLink;
    fxLink.target = targetFor(kTrackId, kSharedId, 0);
    track.macros[1].links.push_back(fxLink);

    tracks.push_back(std::move(track));

    std::vector<AutomationLaneInfo> lanes;
    std::vector<AutomationClipInfo> automationClips;
    migrations::applyParamIndexMigrations(tracks, nullptr, lanes, automationClips, table);

    auto& migrated = tracks.front();

    // The FX-chain device migrated.
    CHECK(getDevice(migrated.chain.fxChainElements.front()).parameters.size() == 3);
    REQUIRE(migrated.macros[1].links.size() == 1);
    CHECK(migrated.macros[1].links[0].target.paramIndex == 1);

    // The other two sections' devices are untouched, parameters and links.
    CHECK(migrated.chain.postFxChainElements.front().device.parameters.size() == 4);
    CHECK(migrated.chain.mixerAnalysisElements.front().device.parameters.size() == 4);
    REQUIRE(migrated.macros[0].links.size() == 1);
    CHECK(migrated.macros[0].links[0].target.paramIndex == 2);
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

// -----------------------------------------------------------------------------
// Choice lists that grew (#3097)
// -----------------------------------------------------------------------------

namespace magda {
namespace {

/// A Filter as an old file saved it: Engine's list ended at Diode (5), and the SVF's Mode list at
/// Notch (3).
DeviceInfo makeOldFilter(DeviceId id, float engine) {
    DeviceInfo device;
    device.id = id;
    device.pluginId = "magda_filter";
    for (int index = 0; index < 7; ++index) {
        ParameterInfo param;
        param.paramIndex = index;
        device.parameters.push_back(param);
    }
    device.parameters[3].currentValue = engine;
    device.parameters[3].maxValue = 5.0f;
    device.parameters[4].maxValue = 3.0f;
    return device;
}

AutomationLaneInfo laneAt(AutomationLaneId id, ControlTarget target, double value) {
    AutomationLaneInfo lane;
    lane.id = id;
    lane.target = std::move(target);
    AutomationPoint point;
    point.value = value;
    lane.absolutePoints.push_back(point);
    return lane;
}

}  // namespace

TEST_CASE("Automation on a grown choice list keeps its choices", "[migration][params][3097]") {
    constexpr TrackId kTrackId = 1;
    constexpr DeviceId kSvf = 1;
    constexpr DeviceId kOberheim = 2;

    std::vector<TrackInfo> tracks(1);
    auto& track = tracks.front();
    track.id = kTrackId;
    track.chain.fxChainElements.push_back(makeOldFilter(kSvf, 0.0f));
    track.chain.fxChainElements.push_back(makeOldFilter(kOberheim, 3.0f));
    track.mods.push_back(ModInfo(0));

    ModLink modeLink;
    modeLink.target = targetFor(kTrackId, kSvf, 4);
    modeLink.amount = 1.0f;
    track.mods[0].links.push_back(modeLink);

    // Diode was 5/5 of Engine; Notch 3/3 and HP 2/3 of the SVF's Mode.
    std::vector<AutomationLaneInfo> lanes{laneAt(1, targetFor(kTrackId, kSvf, 3), 1.0),
                                          laneAt(2, targetFor(kTrackId, kSvf, 4), 2.0 / 3.0),
                                          laneAt(3, targetFor(kTrackId, kOberheim, 4), 2.0 / 3.0)};
    std::vector<AutomationClipInfo> clips(1);
    clips[0].laneId = 2;
    AutomationPoint notch;
    notch.value = 1.0;
    clips[0].points.push_back(notch);

    for (int load = 0; load < 2; ++load) {
        CAPTURE(load);
        migrations::applyChoiceGrowths(tracks, nullptr, lanes, clips);

        CHECK(lanes[0].absolutePoints[0].value * 6.0 == Catch::Approx(5.0));
        CHECK(lanes[1].absolutePoints[0].value * 4.0 == Catch::Approx(2.0));
        CHECK(clips[0].points[0].value * 4.0 == Catch::Approx(3.0));
        CHECK(track.mods[0].links[0].amount == Catch::Approx(0.75f));
        // The Oberheim's modes did not grow.
        CHECK(lanes[2].absolutePoints[0].value == Catch::Approx(2.0 / 3.0));
    }

    const auto& svf = getDevice(track.chain.fxChainElements.front());
    CHECK(svf.parameters[3].maxValue == 6.0f);
    CHECK(svf.parameters[4].maxValue == 4.0f);
}

TEST_CASE("Automation on Poly Synth's slope keeps 24 dB on 4 poles", "[migration][params][2926]") {
    constexpr TrackId kTrackId = 1;
    constexpr DeviceId kSynth = 1;

    std::vector<TrackInfo> tracks(1);
    auto& track = tracks.front();
    track.id = kTrackId;
    DeviceInfo synth;
    synth.id = kSynth;
    synth.pluginId = "magda_polysynth";
    for (int index = 0; index < 44; ++index) {
        ParameterInfo param;
        param.paramIndex = index;
        synth.parameters.push_back(param);
    }
    synth.parameters[29].maxValue = 1.0f;
    track.chain.fxChainElements.push_back(synth);

    std::vector<AutomationLaneInfo> lanes{laneAt(1, targetFor(kTrackId, kSynth, 29), 1.0)};
    std::vector<AutomationClipInfo> clips;
    for (int load = 0; load < 2; ++load) {
        CAPTURE(load);
        migrations::applyChoiceGrowths(tracks, nullptr, lanes, clips);
        CHECK(lanes[0].absolutePoints[0].value * 2.0 == Catch::Approx(1.0));
    }
}

}  // namespace magda
