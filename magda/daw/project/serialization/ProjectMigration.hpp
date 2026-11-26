// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ProjectMigration.hpp
 * @brief Everything the project readers know about older file layouts (#2323).
 *
 * Each reader reads the current layout only. An `upgrade*` call at the top of a
 * reader rewrites an older object's keys into the current ones; the `migrate*` and
 * `read*` calls handle what needs the model, the source pool or the device registry.
 * Every function is a no-op on an object this build wrote.
 */

#pragma once

#include <vector>

#include "../../core/ClipInfo.hpp"
#include "../../core/DeviceInfo.hpp"
#include "../../core/MacroInfo.hpp"
#include "../../core/ModInfo.hpp"
#include "../../core/Source.hpp"
#include "JsonReader.hpp"

namespace magda::project_migration {

/** @brief Older project settings: a palette saved as bare colour strings. */
ser::ObjectReader upgradeProjectSettings(const ser::ObjectReader& project);

/** @brief Pre-clip automation: a plain array of lanes in place of the lanes/clips object. */
juce::var upgradeAutomation(const juce::var& automation);

/**
 * @brief A clip's retired keys, in their current form.
 *
 * Placement saved as start/length fields, the piano roll's row height under its old
 * name, a groove saved by name (#3044), and audio events saved before their fields
 * carried provenance (#2674). @p projectTempo converts a placement saved in seconds.
 */
ser::ObjectReader upgradeClip(const ser::ObjectReader& clip, double projectTempo);

/** @brief Whether @p clip is an audio clip in a schema v1 layout, with no event list. */
bool isLegacyAudioClip(const ser::ObjectReader& clip);

/**
 * @brief Read a v1 audio clip's one source and playback block into a single event.
 *
 * @p legacySources stages the source under a provisional id when non-null; otherwise
 * it is pooled directly, which only the migration tests do.
 */
void readLegacyAudioClip(const ser::ObjectReader& clip, ClipInfo& outClip, double projectTempo,
                         std::vector<Source>* legacySources);

/**
 * @brief A device's retired keys, in their current form.
 *
 * Parameters saved with index -1 or without a value convention, parameter selections
 * saved as array positions (#2638), and a sidechain saved as a bare flag (#2329).
 */
ser::ObjectReader upgradeDevice(const ser::ObjectReader& device);

/** @brief Value fixes on a read device: Utility ranges and gain, and pads kept in its state. */
void migrateDevice(DeviceInfo& device);

/** @brief Pre-#1149 a macro held one `target` and no links: lift it into one. */
void liftLegacyTarget(const ser::ObjectReader& macro, std::vector<MacroLink>& links);
/** @brief The same for a mod, whose link also takes the mod's own `amount`. */
void liftLegacyTarget(const ser::ObjectReader& mod, std::vector<ModLink>& links);

/** @brief Older snap keys on an automation lane. */
ser::ObjectReader upgradeAutomationLane(const ser::ObjectReader& lane);

/** @brief Automation clip timing saved under seconds-era names. */
ser::ObjectReader upgradeAutomationClip(const ser::ObjectReader& clip);

/** @brief A point and its handles with `time` in place of beats. */
ser::ObjectReader upgradeAutomationPoint(const ser::ObjectReader& point);

/**
 * @brief An automation target from before targets were unified.
 *
 * The track id on the target rather than its path, a macro index of its own, and the
 * retired sync-division lane at mod parameter 1.
 */
ser::ObjectReader upgradeAutomationTarget(const ser::ObjectReader& target);

}  // namespace magda::project_migration
