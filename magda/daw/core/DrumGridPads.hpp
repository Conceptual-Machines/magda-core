#pragma once

#include <juce_core/juce_core.h>

#include <memory>

#include "TypeIds.hpp"

namespace magda {

struct ChainInfo;
struct DeviceInfo;
struct RackInfo;

/**
 * A pad-per-chain device's pads, read out of its saved state as chains (#2192).
 *
 * A Drum Grid saves its pads inside its own device state, so the plan compiler
 * cannot expand one the way it expands a rack. The state is already engine
 * neutral; what it is not is typed, so this turns its property bag into the
 * chains the compiler wants.
 *
 * A projection, not a second copy: nothing here is serialized separately and no
 * project file changes shape.
 */

/// The pads saved for `pluginId` in `pluginState`, as a rack of chains.
///
/// Null for a device that is not pad-per-chain, one with no pads saved, and
/// state that cannot be read. Handles both a v2 document and pre-v2 engine XML.
std::unique_ptr<RackInfo> readPadRack(const juce::String& pluginId,
                                      const juce::String& pluginState);

/// Point `device.padRack` at whatever `device.pluginState` currently holds,
/// clearing it for a device with no pads. Parses nothing unless the id has pads.
void refreshPadRack(DeviceInfo& device);

/// True when devices of this type keep their chains as pads.
bool isPadRackDevice(const juce::String& pluginId);

/// The parameter slot a pad's level and pan live in, or -1 when the pad's range
/// starts outside the grid.
///
/// A Drum Grid registers padLevelN and padPanN for a fixed N per pad and reaches
/// them by the pad's bottom note, not by the order its chains were made: a pad
/// added first can hold chain 0 and still drive slot 17. Anything binding a pad
/// to those parameters has to ask the same question the device does.
int padParameterSlot(const ChainInfo& pad);

/// The RackId a pad rack owned by `deviceId` carries.
///
/// Negative, and never INVALID_RACK_ID. Rack ids the app allocates start at 1,
/// so the negative space is free and a pad rack can be keyed and looked up like
/// any other rack without an allocator that does not reach here.
RackId padRackIdFor(DeviceId deviceId);

}  // namespace magda
