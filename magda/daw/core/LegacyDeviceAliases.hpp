#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "ChainNodePath.hpp"
#include "RackInfo.hpp"  // DeviceInfo, RackInfo, ChainElement

namespace magda {

struct TrackInfo;
struct AutomationLaneInfo;
struct AutomationClipInfo;

/**
 * Load-time aliases from the retired stock Tracktion effects onto MAGDA's
 * compiled-Faust successors.
 *
 * Eight Tracktion devices (4-band EQ, Compressor, Delay, Chorus, Phaser,
 * Reverb, Pitch Shift, Lowpass) have compiled-Faust successors, and stayed
 * registered only so older projects would still open. Keeping them addressable
 * meant keeping a Tracktion plugin wrapper per device, so instead each retired
 * type is now an alias: a project naming one loads the compiled device, with
 * its saved parameter values converted into the successor's units.
 *
 * The alias is also the migration. The device is rewritten in place at load, so
 * the retired id leaves the project the next time it is saved and every
 * downstream comparison sees the canonical compiled id.
 *
 * Conversions work in REAL units (Hz, dB, ms, ratio), because that is the only
 * space the two sides share — Tracktion's compressor threshold is a linear gain
 * where the compiled one is dB, its ratio is reciprocal, and its reverb splits
 * wet and dry where the compiled one has a single mix. Parameters with no
 * counterpart (reverb freeze, EQ phase invert, the compressor's sidechain gain)
 * are dropped rather than approximated, and controls the retired device had
 * baked in (its filter topology, its stage count) become fixed slot values so
 * the successor starts where its predecessor left off.
 */
namespace legacy_devices {

/// True when `pluginId` names one of the retired stock Tracktion effects.
bool isRetiredDeviceId(const juce::String& pluginId);

/**
 * @brief Rewrite `device` onto its compiled successor if it names a retired
 *        stock Tracktion type.
 *
 * Replaces `pluginId`, `parameters` and the display name, and drops the state
 * saved for the retired device: it describes a plugin that no longer exists,
 * and every value worth keeping has already been converted into `parameters`.
 * Device-scoped macro and mod links are cleared with it — they address
 * parameter indices that no longer mean the same thing.
 *
 * Idempotent, and a no-op for the overwhelmingly common non-retired device.
 *
 * @return true when the device was rewritten.
 */
bool migrateRetiredDevice(DeviceInfo& device);

/**
 * @brief Migrate every retired device in a track's chains, and drop the
 *        track- and rack-scoped macro / mod links that addressed them.
 *
 * @return the paths of the devices that were migrated, so a caller holding the
 *         automation graph can prune it too.
 */
std::vector<ChainNodePath> migrateRetiredDevicesInTrack(TrackInfo& track);

/**
 * @brief Project-level pass: every track plus the master, and the automation
 *        lanes and clips that addressed a migrated device.
 *
 * Run once on staged data, after tracks and automation are deserialized and
 * before anything is committed to the managers.
 */
void migrateRetiredDevicesInProject(std::vector<TrackInfo>& tracks, TrackInfo* masterTrack,
                                    std::vector<AutomationLaneInfo>& lanes,
                                    std::vector<AutomationClipInfo>& clips);

/**
 * @brief Migrate a loose chain fragment — a chain or rack preset.
 *
 * A fragment carries no surrounding graph and its device ids are unique within
 * it, so the links it owns are matched by device id rather than by path.
 */
void migrateRetiredDevicesInChain(std::vector<ChainElement>& elements);
void migrateRetiredDevicesInRack(RackInfo& rack);

}  // namespace legacy_devices

}  // namespace magda
