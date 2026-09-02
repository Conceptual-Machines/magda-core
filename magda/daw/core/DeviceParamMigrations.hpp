#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <span>
#include <vector>

#include "DeviceInfo.hpp"
#include "RackInfo.hpp"

namespace magda {

struct TrackInfo;
struct AutomationLaneInfo;
struct AutomationClipInfo;

/**
 * paramIndex migrations: what to do when a device's parameter order changes
 * (#2079).
 *
 * A device's parameter ORDER is a compatibility surface. Automation lanes,
 * macro links, mod links and the saved parameter VALUES themselves are all
 * keyed on `paramIndex`, which is nothing but the parameter's position in the
 * device's automatable list. Move a parameter and every old project addressing
 * it moves with it: the file still loads, the project still plays, and a
 * different knob is being driven. That is the worst kind of corruption -
 * it loads without complaint and sounds wrong.
 *
 * `tests/device_param_schema.txt` (#1887) freezes the order and a test fails on
 * any change, which stops the accident. This is the other half: the way to make
 * the change deliberately, and the record of the ones already made.
 *
 * A migration is matched on the device type AND the number of parameters the
 * file saved, which is what identifies the order a file was written against.
 * The order a migrated device ends up in must therefore be a DIFFERENT length
 * from the one that triggered the migration, or every load would migrate the
 * same device again and permute it one more time; a test holds that.
 *
 * Adding one:
 *   1. write `oldToNew`, one entry per index the OLD order had, whose value is
 *      that parameter's index in the NEW order (or `kDropped`);
 *   2. seed any parameter the NEW order added whose default would change how
 *      the project sounds;
 *   3. regenerate `tests/device_param_schema.txt`;
 *   4. add a corpus project that has the old order saved in it, and assert
 *      where its values and links land (tests/test_legacy_corpus.cpp);
 *   5. write it down in the manual's project migrations page - a migration
 *      nobody can point at in writing is a behaviour change users discover on
 *      their own.
 */
namespace device_param_migrations {

/// A parameter the new order does not have. Its value and any link to it are
/// dropped rather than re-pointed: moving a link onto a neighbour is exactly
/// the corruption this file prevents.
inline constexpr int kDropped = -1;

/// A parameter the NEW order added, with the value that reproduces what the old
/// device did. Without this, a parameter whose default is "off" silently
/// switches off a band, a stage or a whole effect that used to be on.
struct SeededParam {
    int index = -1;
    const char* name = nullptr;
    /// Real value, in the successor's own units - the same convention the
    /// retired-device aliases use (`LegacyDeviceAliases.hpp`).
    float value = 0.0f;
};

/// One device type's renumbering, from one specific old order.
struct ParamIndexMigration {
    /// Canonical device id (`DeviceInfo::pluginId`).
    const char* deviceType = nullptr;
    /// What changed, for the failure message and the manual.
    const char* reason = nullptr;
    /// How many parameters the old order had. This is what identifies a file as
    /// belonging to that order.
    int savedParamCount = 0;
    /// Indexed by the paramIndex as SAVED; the value is the index today.
    std::span<const int> oldToNew;
    /// Parameters the new order added.
    std::span<const SeededParam> seeded;
};

using MigrationTable = std::vector<ParamIndexMigration>;

/// The migrations this build ships.
const MigrationTable& shippedMigrations();

/// The migration that applies to `device` as it was saved, or null.
const ParamIndexMigration* findMigration(const DeviceInfo& device, const MigrationTable& table);

inline const ParamIndexMigration* findMigration(const DeviceInfo& device) {
    return findMigration(device, shippedMigrations());
}

/// The migration that applies to a saved parameter record carrying
/// @p savedParamCount entries for @p deviceType, or null. For state that
/// arrives WITHOUT a model parameter array - an old preset's document, an
/// imported chain - where the count identifying the saved order is the
/// record's own length rather than `DeviceInfo::parameters.size()`.
const ParamIndexMigration* findMigrationForSavedCount(const juce::String& deviceType,
                                                      int savedParamCount,
                                                      const MigrationTable& table);

inline const ParamIndexMigration* findMigrationForSavedCount(const juce::String& deviceType,
                                                             int savedParamCount) {
    return findMigrationForSavedCount(deviceType, savedParamCount, shippedMigrations());
}

/**
 * The index `savedIndex` becomes under `migration`.
 *
 * Nullopt means whatever addressed it has to go: either the new order retired
 * that parameter, or the index is past the end of the old order, which means
 * the file and the migration disagree about what that order was.
 */
std::optional<int> migratedParamIndex(const ParamIndexMigration& migration, int savedIndex);

/**
 * Rewrite a whole project: every device's saved parameter values and panel
 * selections, every macro and mod link on tracks, racks and devices, and every
 * automation lane (with its clips).
 *
 * Runs after the retired-device aliases (`LegacyDeviceAliases.hpp`), which can
 * change what a device IS: a device rewritten onto its successor is already the
 * successor by the time its indices are looked at here.
 */
void applyParamIndexMigrations(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                               std::vector<AutomationLaneInfo>& lanes,
                               std::vector<AutomationClipInfo>& automationClips,
                               const MigrationTable& table);

inline void applyParamIndexMigrations(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                                      std::vector<AutomationLaneInfo>& lanes,
                                      std::vector<AutomationClipInfo>& automationClips) {
    applyParamIndexMigrations(tracks, masterTrack, lanes, automationClips, shippedMigrations());
}

// --- Fragments -------------------------------------------------------------
//
// A preset carries no surrounding project, and its device ids are unique inside
// it, so a fragment fixes its own links by id.

void migrateDevicePreset(DeviceInfo& device, const MigrationTable& table);
void migrateChainPreset(std::vector<ChainElement>& elements, const MigrationTable& table);
void migrateRackPreset(RackInfo& rack, const MigrationTable& table);

inline void migrateDevicePreset(DeviceInfo& device) {
    migrateDevicePreset(device, shippedMigrations());
}
inline void migrateChainPreset(std::vector<ChainElement>& elements) {
    migrateChainPreset(elements, shippedMigrations());
}
inline void migrateRackPreset(RackInfo& rack) {
    migrateRackPreset(rack, shippedMigrations());
}

}  // namespace device_param_migrations
}  // namespace magda
