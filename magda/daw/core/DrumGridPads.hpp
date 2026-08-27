#pragma once

#include <juce_core/juce_core.h>

#include <memory>

#include "DeviceInfo.hpp"
#include "TypeIds.hpp"

namespace magda {

struct ChainInfo;
struct RackInfo;

/**
 * A pad-per-chain device's pads, as model state (#2207).
 *
 * A Drum Grid's pads are a rack of chains the device owns: `DeviceInfo::pads`.
 * The model holds them, the project file saves them, every edit writes them,
 * and the plugin is filled from them the way `RackSyncManager` fills a
 * `te::RackType` from a `RackInfo`. One direction, so nothing can drift.
 *
 * They were a projection of the plugin's saved state until #2207 (#2192, #2200,
 * #2205): decoded from `pluginState` and rebuilt on every capture, which made
 * the plugin the truth and the model a lagging mirror of it. A pad added since
 * the last capture was missing from the plan, a pad removed since was still in
 * it, and neither showed under Tracktion because the plugin played itself.
 * `readLegacyPads()` is all that is left of that reader, and it runs once, at
 * load, on a project saved before the pads moved.
 */

/// The pads a Drum Grid has, and the note its first one answers to.
constexpr int kPadCount = 64;
constexpr int kPadBaseNote = 24;

/// How many outputs a pad can be sent to: 0 is the grid's own mix, 1 upwards
/// are multi-out buses. `DrumGridPlugin::maxBusOutputs` is the same number,
/// from the same limit (a TE RackType carries 64 audio pins, so 32 stereo
/// pairs). Here as well because the model is what the plan compiler routes
/// from, and the two engines have to agree on which buses exist: the live
/// plugin clamps what it is given, and the plan takes the model's value as it
/// finds it, so an out-of-range one reaches no track and silences its pads.
constexpr int kPadBusCount = 32;

/// True when devices of this type keep their chains as pads.
bool isPadRackDevice(const juce::String& pluginId);

/// True when @p rackId names a pad rack rather than one the app allocated.
bool isPadRackId(RackId rackId);

/// The RackId a pad rack owned by `deviceId` carries.
///
/// Negative, and never INVALID_RACK_ID. Rack ids the app allocates start at 1,
/// so the negative space is free and a pad rack can be keyed and looked up like
/// any other rack without an allocator that does not reach here.
///
/// Derived rather than stored, so a device that is copied or re-keyed cannot
/// carry another device's rack id: `stampPadRackId()` re-derives it wherever a
/// DeviceId is assigned.
RackId padRackIdFor(DeviceId deviceId);

/// The MIDI note pad @p padIndex answers to.
int padNoteFor(int padIndex);

/// The parameter slot a pad's level and pan live in, or -1 when the pad's range
/// starts outside the grid. Also the pad's index on the grid.
///
/// A Drum Grid registers padLevelN and padPanN for a fixed N per pad and reaches
/// them by the pad's bottom note, not by the order its chains were made: a pad
/// added first can hold chain 0 and still drive slot 17. Anything binding a pad
/// to those parameters has to ask the same question the device does.
int padParameterSlot(const ChainInfo& pad);

/// True when any of @p device's pads plays out of a multi-out bus rather than
/// the device's own mix.
///
/// Asked before a structural move that would take the device somewhere buses do
/// not work. Refusing the move is what keeps the assignment: normalising it
/// afterwards would be undone by nothing, because the move is one undoable step
/// and the normalisation would not be part of it (#2211).
bool anyPadOnABus(const DeviceInfo& device);

/// Point `device.pads->id` at whatever `device.id` currently is. No-op for a
/// device with no pads.
void stampPadRackId(DeviceInfo& device);

/// @p device's pads, made (empty) if it has none. Only call for a pad-per-chain
/// device; anything else has no pads to give.
RackInfo& ensurePads(DeviceInfo& device);

/// The pad chain covering @p padIndex, or null.
ChainInfo* findPadChain(RackInfo& pads, int padIndex);
const ChainInfo* findPadChain(const RackInfo& pads, int padIndex);

/// The pad chain covering @p padIndex, made if it is not there yet.
///
/// A new one answers to that pad's note alone and is rooted on it, which is
/// what a sampler mapped at C0 needs to play from whichever pad triggered it.
ChainInfo& ensurePadChain(RackInfo& pads, int padIndex);

/// The next free chain id in @p pads.
///
/// Pad chain ids are rack-local and stay with the pad across saves, so nothing
/// that names one (a macro, a mod, an op key) has to be remapped.
ChainId nextPadChainId(const RackInfo& pads);

/// The device a pad sampler needs to play @p samplePath, rooted on @p rootNote.
///
/// Model state, not a live plugin: the sample path and the root note are the
/// sampler's own saved properties, so a pad built this way needs nothing
/// re-derived after a save and reload.
DeviceInfo padSamplerDevice(const juce::String& samplePath, int rootNote);

/// The pads a project saved before #2207 kept inside `pluginState`, as a rack.
///
/// Null for a device that is not pad-per-chain, one with no pads saved, and
/// state that cannot be read. Handles both a v2 document and pre-v2 engine XML.
std::unique_ptr<RackInfo> readLegacyPads(const juce::String& pluginId,
                                         const juce::String& pluginState);

/// Move a pre-#2207 device's pads out of its plugin state and into the model.
///
/// No-op once the device has pads, so a project saved since is never re-read
/// from the copy its plugin state still carries. A pad plugin saved before pad
/// ids existed arrives with `INVALID_DEVICE_ID`; ids are allocated once the
/// whole project is loaded, by `TrackManager::allocatePadDeviceIds()`, which is
/// also where a colliding one would be caught.
void migrateLegacyPads(DeviceInfo& device);

}  // namespace magda
