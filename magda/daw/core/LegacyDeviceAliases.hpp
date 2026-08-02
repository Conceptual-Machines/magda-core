#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

#include "ChainNodePath.hpp"
#include "RackInfo.hpp"  // DeviceInfo, RackInfo, ChainElement

namespace magda {

struct TrackInfo;
struct AutomationLaneInfo;
struct AutomationClipInfo;

/**
 * Load-time aliases from the retired stock Tracktion effects onto their MAGDA
 * successors.
 *
 * Nine Tracktion devices (4-band EQ, Compressor, Delay, Chorus, Phaser, Reverb,
 * Pitch Shift, Lowpass, IR Reverb) have MAGDA successors, and stayed registered
 * only so older projects would still open. Keeping them addressable meant
 * keeping a Tracktion plugin wrapper per device, so instead each retired type is
 * now an alias: a project naming one loads the successor, with its saved
 * parameter values converted into that device's units.
 *
 * The alias is also the migration. The device is rewritten in place at load, so
 * the retired id leaves the project the next time it is saved and every
 * downstream comparison sees the canonical successor id.
 *
 * Conversions work in REAL units (Hz, dB, ms, ratio), because that is the only
 * space the two sides share — Tracktion's compressor threshold is a linear gain
 * where the compiled one is dB, its ratio is reciprocal, and its reverb splits
 * wet and dry where the compiled one has a single mix. Parameters with no
 * counterpart (reverb freeze, EQ phase invert, the compressor's sidechain gain)
 * are dropped rather than approximated, and controls the retired device had
 * baked in (its filter topology, its stage count) become fixed slot values so
 * the successor starts where its predecessor left off.
 *
 * One of them, the IR Reverb, needs more than a parameter table, in two ways.
 *
 * It carries STATE: its impulse response is a binary blob in the
 * saved device state, not a parameter, and the native convolution device reads
 * it back under the same property name. A retired device may therefore name
 * state properties its successor keeps verbatim, which is what makes a migrated
 * project come back with its IR still loaded.
 *
 * It also keeps PARAMETER IDENTITY: same five parameters, same order, same
 * normalised curves, so saved automation, macro links and mod links still
 * address what they addressed before and survive the rewrite. For the other
 * eight the successor's parameter list is a different shape, so their links are
 * dropped rather than re-pointed onto indices that no longer mean the same
 * thing.
 */
namespace legacy_devices {

/// The MAGDA device that replaced `pluginId`, or an empty string when
/// `pluginId` does not name a retired stock Tracktion effect.
juce::String retiredDeviceSuccessor(const juce::String& pluginId);

/**
 * @brief True when the successor's parameter indices mean exactly what the
 *        retired device's did.
 *
 * Only the IR Reverb qualifies. Callers holding saved links or automation use
 * this to decide whether a migration invalidates them: false means the indices
 * were renumbered and anything addressing them has to go.
 */
bool retiredDeviceKeepsParameterIdentity(const juce::String& pluginId);

/// One converted value, in the successor's real units, for the slot it belongs
/// to. `name` is the successor's name for that slot.
struct RetiredSlotValue {
    int slot = -1;
    const char* name = nullptr;
    float value = 0.0f;
};

/// Answers with a retired device's saved value for one of its engine property
/// names, or a void var when the state does not carry it.
using PropertyReader = std::function<juce::var(const char* property)>;

/**
 * @brief Convert a retired device saved as the engine's own plugin tree.
 *
 * `migrateRetiredDevice` handles anything MAGDA models as a `DeviceInfo`. A
 * device embedded inside another one — an FX chain on a Drum Grid pad — was
 * never a `DeviceInfo`: it is a nested plugin tree the owning device restores
 * itself, where values are named properties rather than parameter indices.
 * This reads that shape and produces the same converted slot values.
 */
std::vector<RetiredSlotValue> convertRetiredDeviceState(const juce::String& retiredType,
                                                        const PropertyReader& readProperty);

/// The engine property names a retired device saved, so a caller that has just
/// converted a tree can clear what it consumed.
std::vector<const char*> retiredDeviceProperties(const juce::String& retiredType);

/**
 * @brief Rewrite `device` onto its MAGDA successor if it names a retired stock
 *        Tracktion type.
 *
 * Replaces `pluginId`, `parameters` and the display name. The state saved for
 * the retired device is reduced to the properties the successor keeps verbatim
 * (the IR Reverb's impulse response) and otherwise dropped: it describes a
 * plugin that no longer exists, and every value worth keeping has already been
 * converted into `parameters`. Device-scoped macro and mod links go with it,
 * unless the successor kept parameter identity.
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
