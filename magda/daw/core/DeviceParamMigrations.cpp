#include "DeviceParamMigrations.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>

#include "AutomationInfo.hpp"
#include "ChainWalk.hpp"
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

/**
 * Which migration each device needs, collected BEFORE any of them run: a
 * migration is matched on the saved parameter count, and migrating a device
 * changes that count.
 *
 * A project keys them by PATH. A `DeviceId` is not unique across a project: the
 * main FX chain, the post-FX stage and the mixer-analysis rail each allocate
 * from their own counter (`ensureDeviceIdAbove`, `ensurePostFxDeviceIdAbove`,
 * `ensureMixerAnalysisDeviceIdAbove`), so one number can address three
 * different devices in the same project. Keying by id would let one section's
 * device be run through another section's migration - the exact silent
 * repointing this file exists to prevent - and would let two entries overwrite
 * each other in the map.
 *
 * A preset is a fragment with no project around it and one section in it, so
 * its ids ARE unique inside it and it keys by id. `LegacyDeviceAliases` makes
 * the same split, and a saved `ControlTarget` inside a preset is matched the
 * same way.
 */
using MigrationsByPath = std::map<ChainNodePath, const ParamIndexMigration*>;
using MigrationsById = std::map<DeviceId, const ParamIndexMigration*>;

/// A device and the path saved links address it by.
using DeviceAtPath = std::pair<ChainNodePath, DeviceInfo*>;

/// Every device on a track, each with the path its links address it by.
std::vector<DeviceAtPath> devicesInTrack(TrackInfo& track) {
    std::vector<DeviceAtPath> devices;
    // Pads skipped: these migrations rewrite parameters saved against a
    // DeviceInfo in the chain model, and the Drum Grid covers its own retired
    // nested plugins through adoptRetiredNestedPluginTree.
    //
    // The walk spells a top-level device the flat way the control graph stored
    // it, which this had to remember for itself (#2204).
    chain_walk::forEachDevice(track.chain.fxChainElements, ChainNodePath::trackLevel(track.id),
                              chain_walk::Pads::Skip,
                              [&devices](DeviceInfo& device, const ChainNodePath& path) {
                                  devices.emplace_back(path, &device);
                              });
    for (auto& element : track.chain.postFxChainElements)
        devices.emplace_back(ChainNodePath::postFxDevice(track.id, element.device.id),
                             &element.device);
    for (auto& element : track.chain.mixerAnalysisElements)
        devices.emplace_back(ChainNodePath::mixerAnalysisDevice(track.id, element.device.id),
                             &element.device);
    return devices;
}

// --- Fragments (presets), matched by id ------------------------------------

void collectFragmentElements(std::vector<ChainElement>& elements, std::vector<DeviceInfo*>& out);

void collectFragmentRack(RackInfo& rack, std::vector<DeviceInfo*>& out) {
    for (auto& chain : rack.chains)
        collectFragmentElements(chain.elements, out);
}

void collectFragmentElements(std::vector<ChainElement>& elements, std::vector<DeviceInfo*>& out) {
    for (auto& element : elements) {
        if (isDevice(element))
            out.push_back(&getDevice(element));
        else if (isRack(element))
            collectFragmentRack(getRack(element), out);
    }
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
    for (auto& element : track.chain.mixerAnalysisElements)
        visit(element.device.macros, element.device.mods);
}

/// Nullopt when the link should be dropped; the index back when nothing
/// applies. `lookup` answers with the migration for a target, or null.
template <typename Lookup>
std::optional<int> migratedTargetIndex(const ControlTarget& target, const Lookup& lookup) {
    if (target.kind != ControlTarget::Kind::PluginParam)
        return target.paramIndex;

    const auto* migration = lookup(target);
    if (migration == nullptr)
        return target.paramIndex;

    return migratedParamIndex(*migration, target.paramIndex);
}

/// Rewrite what still exists, drop what does not.
template <typename LinkArray, typename Lookup>
void migrateLinkList(LinkArray& links, const Lookup& lookup) {
    LinkArray kept;
    kept.reserve(links.size());
    for (auto& link : links) {
        const auto mapped = migratedTargetIndex(link.target, lookup);
        if (!mapped)
            continue;
        link.target.paramIndex = *mapped;
        kept.push_back(link);
    }
    links = std::move(kept);
}

template <typename Lookup>
void migrateOwnerLinks(MacroArray& macros, ModArray& mods, const Lookup& lookup) {
    for (auto& macro : macros)
        migrateLinkList(macro.links, lookup);
    for (auto& mod : mods)
        migrateLinkList(mod.links, lookup);
}

auto lookupByPath(const MigrationsByPath& found) {
    return [&found](const ControlTarget& target) -> const ParamIndexMigration* {
        const auto entry = found.find(target.devicePath);
        return entry == found.end() ? nullptr : entry->second;
    };
}

auto lookupById(const MigrationsById& found) {
    return [&found](const ControlTarget& target) -> const ParamIndexMigration* {
        const auto entry = found.find(target.devicePath.getDeviceId());
        return entry == found.end() ? nullptr : entry->second;
    };
}

}  // namespace

const MigrationTable& shippedMigrations() {
    return table();
}

const ParamIndexMigration* findMigrationForSavedCount(const juce::String& deviceType,
                                                      int savedParamCount,
                                                      const MigrationTable& table) {
    for (const auto& migration : table) {
        if (deviceType != migration.deviceType)
            continue;
        if (savedParamCount != migration.savedParamCount)
            continue;  // a different order, or already migrated
        return &migration;
    }
    return nullptr;
}

const ParamIndexMigration* findMigration(const DeviceInfo& device, const MigrationTable& table) {
    return findMigrationForSavedCount(device.pluginId, static_cast<int>(device.parameters.size()),
                                      table);
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

    // Collect every device with its path first: a migration is matched on the
    // parameter count the file saved, so nothing may be migrated until the whole
    // project has been read.
    std::vector<DeviceAtPath> devices;
    for (auto& track : tracks) {
        auto onTrack = devicesInTrack(track);
        devices.insert(devices.end(), onTrack.begin(), onTrack.end());
    }
    if (masterTrack != nullptr) {
        auto onMaster = devicesInTrack(*masterTrack);
        devices.insert(devices.end(), onMaster.begin(), onMaster.end());
    }

    MigrationsByPath found;
    for (const auto& [path, device] : devices)
        if (const auto* migration = findMigration(*device, table))
            found[path] = migration;

    if (found.empty())
        return;

    for (const auto& [path, device] : devices) {
        const auto entry = found.find(path);
        if (entry != found.end() && entry->second != nullptr)
            migrateDeviceParameters(*device, *entry->second);
    }

    const auto lookup = lookupByPath(found);

    const auto migrateLinks = [&lookup](TrackInfo& track) {
        forEachLinkOwnerInTrack(track, [&lookup](MacroArray& macros, ModArray& mods) {
            migrateOwnerLinks(macros, mods, lookup);
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
        const auto mapped = migratedTargetIndex(lane.target, lookup);
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

namespace {

/// Migrate a preset fragment: its devices, then the links its own macros and
/// mods hold. Matched by id, which is unique inside one fragment.
void migrateFragment(const std::vector<DeviceInfo*>& devices, const MigrationTable& table,
                     const std::function<void(const MigrationsById&)>& migrateLinks) {
    MigrationsById found;
    for (auto* device : devices)
        if (const auto* migration = findMigration(*device, table))
            found[device->id] = migration;

    if (found.empty())
        return;

    for (auto* device : devices) {
        const auto entry = found.find(device->id);
        if (entry != found.end() && entry->second != nullptr)
            migrateDeviceParameters(*device, *entry->second);
    }

    migrateLinks(found);
}

}  // namespace

void migrateDevicePreset(DeviceInfo& device, const MigrationTable& table) {
    if (table.empty())
        return;

    migrateFragment({&device}, table, [&device](const MigrationsById& found) {
        migrateOwnerLinks(device.macros, device.mods, lookupById(found));
    });
}

void migrateChainPreset(std::vector<ChainElement>& elements, const MigrationTable& table) {
    if (table.empty())
        return;

    std::vector<DeviceInfo*> devices;
    collectFragmentElements(elements, devices);

    migrateFragment(devices, table, [&elements](const MigrationsById& found) {
        forEachLinkOwnerElements(elements, [&found](MacroArray& macros, ModArray& mods) {
            migrateOwnerLinks(macros, mods, lookupById(found));
        });
    });
}

void migrateRackPreset(RackInfo& rack, const MigrationTable& table) {
    if (table.empty())
        return;

    std::vector<DeviceInfo*> devices;
    collectFragmentRack(rack, devices);

    migrateFragment(devices, table, [&rack](const MigrationsById& found) {
        forEachLinkOwnerRack(rack, [&found](MacroArray& macros, ModArray& mods) {
            migrateOwnerLinks(macros, mods, lookupById(found));
        });
    });
}

}  // namespace magda::device_param_migrations
