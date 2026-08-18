#include "DeviceParamMigrations.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>

#include "AutomationInfo.hpp"
#include "TrackInfo.hpp"

namespace magda::device_param_migrations {
namespace {

// --- magda_eq: 33 slots -> 41 ------------------------------------------------
//
// The compiled EQ shipped with four slots per band (type, freq, gain, Q) and
// every band always running. The per-band Enabled switch was added later, at
// the FRONT of each band, so every slot after the first moved: an EQ saved by
// 0.8.0 loads with its band 1 filter type in the Enabled slot, its frequency in
// the type slot, and so on down the device.
//
// Bands default to disabled today, so the migration also has to switch on the
// eight bands the old device was running, or an old project comes back with a
// flat EQ where it had one that shaped the sound.
constexpr int kEqSlotsPerBandOld = 4;
constexpr int kEqSlotsPerBandNew = 5;
constexpr int kEqBandCount = 8;
constexpr int kEqOldCount = kEqBandCount * kEqSlotsPerBandOld + 1;  // + output trim

constexpr std::array<int, kEqOldCount> makeEqMapping() {
    std::array<int, kEqOldCount> mapping{};
    for (int band = 0; band < kEqBandCount; ++band)
        for (int slot = 0; slot < kEqSlotsPerBandOld; ++slot)
            mapping[static_cast<size_t>(band * kEqSlotsPerBandOld + slot)] =
                band * kEqSlotsPerBandNew + 1 + slot;
    mapping[kEqOldCount - 1] = kEqBandCount * kEqSlotsPerBandNew;  // Output
    return mapping;
}
constexpr auto kEqOldToNew = makeEqMapping();

constexpr std::array<SeededParam, kEqBandCount> makeEqSeeds() {
    std::array<SeededParam, kEqBandCount> seeds{};
    constexpr std::array<const char*, kEqBandCount> names{
        "Band 1 Enabled", "Band 2 Enabled", "Band 3 Enabled", "Band 4 Enabled",
        "Band 5 Enabled", "Band 6 Enabled", "Band 7 Enabled", "Band 8 Enabled"};
    for (int band = 0; band < kEqBandCount; ++band)
        seeds[static_cast<size_t>(band)] = {band * kEqSlotsPerBandNew,
                                            names[static_cast<size_t>(band)], 1.0f};
    return seeds;
}
constexpr auto kEqSeeds = makeEqSeeds();

// --- magda_limiter: 7 slots -> 4 --------------------------------------------
//
// Threshold, Attack, Hold, Release, Mix, Output, Autogain became Threshold,
// Attack, Release, Output. Hold, Mix and Autogain went; Release and Output
// moved down onto the indices Hold and Mix had vacated, so an old project's
// release time was landing on Hold and its output trim on Mix.
constexpr std::array<int, 7> kLimiterOldToNew = {0, 1, kDropped, 2, kDropped, 3, kDropped};

const MigrationTable& table() {
    static const MigrationTable migrations = {
        {"magda_eq", "the per-band Enabled switch was inserted at the front of every band",
         kEqOldCount, kEqOldToNew, kEqSeeds},
        {"magda_limiter",
         "Hold, Mix and Autogain were removed from the middle of the list",
         static_cast<int>(kLimiterOldToNew.size()),
         kLimiterOldToNew,
         {}},
    };
    return migrations;
}

// --- Applying ---------------------------------------------------------------

/// Rewrite one device's own saved indices.
void migrateDeviceParameters(DeviceInfo& device, const ParamIndexMigration& migration) {
    std::vector<ParameterInfo> migrated;
    migrated.reserve(device.parameters.size() + migration.seeded.size());

    for (auto& param : device.parameters) {
        const auto mapped = migratedParamIndex(migration, param.paramIndex);
        if (!mapped)
            continue;  // the new order has no such parameter
        param.paramIndex = *mapped;
        migrated.push_back(param);
    }

    for (const auto& seed : migration.seeded) {
        ParameterInfo param;
        param.paramIndex = seed.index;
        param.name = seed.name;
        param.currentValue = seed.value;
        migrated.push_back(std::move(param));
    }

    std::sort(migrated.begin(), migrated.end(), [](const ParameterInfo& a, const ParameterInfo& b) {
        return a.paramIndex < b.paramIndex;
    });
    device.parameters = std::move(migrated);

    const auto migrateIndices = [&migration](std::vector<int>& indices) {
        std::vector<int> kept;
        kept.reserve(indices.size());
        for (int index : indices)
            if (const auto mapped = migratedParamIndex(migration, index))
                kept.push_back(*mapped);
        indices = std::move(kept);
    };

    migrateIndices(device.visibleParameters);
    migrateIndices(device.miniMixerParameters);
    migrateIndices(device.aiSoundDesignerParameters);
}

/// Which migration each device needs, collected BEFORE any of them run: a
/// migration is matched on the saved parameter count, and migrating a device
/// changes that count.
using MigrationsById = std::map<DeviceId, const ParamIndexMigration*>;

void collectElements(const std::vector<ChainElement>& elements, const MigrationTable& table,
                     MigrationsById& found);

void collectRack(const RackInfo& rack, const MigrationTable& table, MigrationsById& found) {
    for (const auto& chain : rack.chains)
        collectElements(chain.elements, table, found);
}

void collectElements(const std::vector<ChainElement>& elements, const MigrationTable& table,
                     MigrationsById& found) {
    for (const auto& element : elements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            if (const auto* migration = findMigration(device, table))
                found[device.id] = migration;
        } else if (isRack(element)) {
            collectRack(getRack(element), table, found);
        }
    }
}

void collectTrack(const TrackInfo& track, const MigrationTable& table, MigrationsById& found) {
    collectElements(track.chain.fxChainElements, table, found);
    for (const auto& element : track.chain.postFxChainElements)
        if (const auto* migration = findMigration(element.device, table))
            found[element.device.id] = migration;
    for (const auto& element : track.chain.mixerAnalysisElements)
        if (const auto* migration = findMigration(element.device, table))
            found[element.device.id] = migration;
}

void forEachDeviceElements(std::vector<ChainElement>& elements,
                           const std::function<void(DeviceInfo&)>& visit);

void forEachDeviceRack(RackInfo& rack, const std::function<void(DeviceInfo&)>& visit) {
    for (auto& chain : rack.chains)
        forEachDeviceElements(chain.elements, visit);
}

void forEachDeviceElements(std::vector<ChainElement>& elements,
                           const std::function<void(DeviceInfo&)>& visit) {
    for (auto& element : elements) {
        if (isDevice(element))
            visit(getDevice(element));
        else if (isRack(element))
            forEachDeviceRack(getRack(element), visit);
    }
}

void forEachDeviceInTrack(TrackInfo& track, const std::function<void(DeviceInfo&)>& visit) {
    forEachDeviceElements(track.chain.fxChainElements, visit);
    for (auto& element : track.chain.postFxChainElements)
        visit(element.device);
    for (auto& element : track.chain.mixerAnalysisElements)
        visit(element.device);
}

void forEachLinkOwnerElements(std::vector<ChainElement>& elements,
                              const std::function<void(MacroArray&, ModArray&)>& visit);

void forEachLinkOwnerRack(RackInfo& rack,
                          const std::function<void(MacroArray&, ModArray&)>& visit) {
    visit(rack.macros, rack.mods);
    for (auto& chain : rack.chains)
        forEachLinkOwnerElements(chain.elements, visit);
}

void forEachLinkOwnerElements(std::vector<ChainElement>& elements,
                              const std::function<void(MacroArray&, ModArray&)>& visit) {
    for (auto& element : elements) {
        if (isDevice(element)) {
            auto& device = getDevice(element);
            visit(device.macros, device.mods);
        } else if (isRack(element)) {
            forEachLinkOwnerRack(getRack(element), visit);
        }
    }
}

void forEachLinkOwnerInTrack(TrackInfo& track,
                             const std::function<void(MacroArray&, ModArray&)>& visit) {
    visit(track.macros, track.mods);
    forEachLinkOwnerElements(track.chain.fxChainElements, visit);
    for (auto& element : track.chain.postFxChainElements)
        visit(element.device.macros, element.device.mods);
}

/// Nullopt when the link should be dropped; the index back when nothing applies.
std::optional<int> migratedTargetIndex(const ControlTarget& target, const MigrationsById& found) {
    if (target.kind != ControlTarget::Kind::PluginParam)
        return target.paramIndex;

    const auto entry = found.find(target.devicePath.getDeviceId());
    if (entry == found.end() || entry->second == nullptr)
        return target.paramIndex;

    return migratedParamIndex(*entry->second, target.paramIndex);
}

/// Rewrite what still exists, drop what does not.
template <typename LinkArray> void migrateLinkList(LinkArray& links, const MigrationsById& found) {
    LinkArray kept;
    kept.reserve(links.size());
    for (auto& link : links) {
        const auto mapped = migratedTargetIndex(link.target, found);
        if (!mapped)
            continue;
        link.target.paramIndex = *mapped;
        kept.push_back(link);
    }
    links = std::move(kept);
}

void migrateOwnerLinks(MacroArray& macros, ModArray& mods, const MigrationsById& found) {
    for (auto& macro : macros)
        migrateLinkList(macro.links, found);
    for (auto& mod : mods)
        migrateLinkList(mod.links, found);
}

}  // namespace

const MigrationTable& shippedMigrations() {
    return table();
}

const ParamIndexMigration* findMigration(const DeviceInfo& device, const MigrationTable& table) {
    for (const auto& migration : table) {
        if (device.pluginId != migration.deviceType)
            continue;
        if (static_cast<int>(device.parameters.size()) != migration.savedParamCount)
            continue;  // a different order, or already migrated
        return &migration;
    }
    return nullptr;
}

std::optional<int> migratedParamIndex(const ParamIndexMigration& migration, int savedIndex) {
    if (savedIndex < 0)
        return savedIndex;

    // Past the end of the order the migration describes: the file and the
    // migration disagree about what that order was, and guessing which
    // parameter was meant is the corruption this file exists to prevent.
    if (savedIndex >= static_cast<int>(migration.oldToNew.size()))
        return std::nullopt;

    const int mapped = migration.oldToNew[static_cast<size_t>(savedIndex)];
    if (mapped == kDropped)
        return std::nullopt;
    return mapped;
}

void applyParamIndexMigrations(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                               std::vector<AutomationLaneInfo>& lanes,
                               std::vector<AutomationClipInfo>& automationClips,
                               const MigrationTable& table) {
    if (table.empty())
        return;

    MigrationsById found;
    for (const auto& track : tracks)
        collectTrack(track, table, found);
    if (masterTrack != nullptr)
        collectTrack(*masterTrack, table, found);

    if (found.empty())
        return;

    const auto migrateDevices = [&found](TrackInfo& track) {
        forEachDeviceInTrack(track, [&found](DeviceInfo& device) {
            const auto entry = found.find(device.id);
            if (entry != found.end() && entry->second != nullptr)
                migrateDeviceParameters(device, *entry->second);
        });
    };
    for (auto& track : tracks)
        migrateDevices(track);
    if (masterTrack != nullptr)
        migrateDevices(*masterTrack);

    const auto migrateLinks = [&found](TrackInfo& track) {
        forEachLinkOwnerInTrack(track, [&found](MacroArray& macros, ModArray& mods) {
            migrateOwnerLinks(macros, mods, found);
        });
    };
    for (auto& track : tracks)
        migrateLinks(track);
    if (masterTrack != nullptr)
        migrateLinks(*masterTrack);

    // A lane whose parameter is gone goes with its clips: leaving it behind
    // would leave a curve writing to whatever now sits at that index.
    std::set<AutomationLaneId> droppedLanes;
    for (auto& lane : lanes) {
        const auto mapped = migratedTargetIndex(lane.target, found);
        if (!mapped) {
            droppedLanes.insert(lane.id);
            continue;
        }
        lane.target.paramIndex = *mapped;
    }

    if (droppedLanes.empty())
        return;

    std::erase_if(lanes, [&droppedLanes](const AutomationLaneInfo& lane) {
        return droppedLanes.count(lane.id) > 0;
    });
    std::erase_if(automationClips, [&droppedLanes](const AutomationClipInfo& clip) {
        return droppedLanes.count(clip.laneId) > 0;
    });
}

void migrateDevicePreset(DeviceInfo& device, const MigrationTable& table) {
    const auto* migration = findMigration(device, table);
    if (migration == nullptr)
        return;

    MigrationsById found{{device.id, migration}};
    migrateDeviceParameters(device, *migration);
    migrateOwnerLinks(device.macros, device.mods, found);
}

void migrateChainPreset(std::vector<ChainElement>& elements, const MigrationTable& table) {
    if (table.empty())
        return;

    MigrationsById found;
    collectElements(elements, table, found);
    if (found.empty())
        return;

    forEachDeviceElements(elements, [&found](DeviceInfo& device) {
        const auto entry = found.find(device.id);
        if (entry != found.end() && entry->second != nullptr)
            migrateDeviceParameters(device, *entry->second);
    });
    forEachLinkOwnerElements(elements, [&found](MacroArray& macros, ModArray& mods) {
        migrateOwnerLinks(macros, mods, found);
    });
}

void migrateRackPreset(RackInfo& rack, const MigrationTable& table) {
    if (table.empty())
        return;

    MigrationsById found;
    collectRack(rack, table, found);
    if (found.empty())
        return;

    forEachDeviceRack(rack, [&found](DeviceInfo& device) {
        const auto entry = found.find(device.id);
        if (entry != found.end() && entry->second != nullptr)
            migrateDeviceParameters(device, *entry->second);
    });
    forEachLinkOwnerRack(rack, [&found](MacroArray& macros, ModArray& mods) {
        migrateOwnerLinks(macros, mods, found);
    });
}

}  // namespace magda::device_param_migrations
