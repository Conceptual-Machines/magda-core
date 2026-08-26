#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace magda {

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

}  // namespace magda
