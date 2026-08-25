#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace magda {

struct DeviceInfo;
struct RackInfo;

/**
 * Reading a pad-per-chain device's pads out of its saved state (#2192).
 *
 * A Drum Grid's pads are saved inside the device's own state rather than in the
 * chain model, so nothing outside the device could see them: the plan compiler
 * could not expand a Drum Grid the way it expands a rack, and the whole device
 * stopped at a Device op that no native engine could bind.
 *
 * The pads are not engine XML any more. A v2 device state document is
 * MAGDA-defined and engine-neutral, and its `root` is where a device's
 * non-parameter state lives -- drum-pad chains included. What is missing is not
 * neutral storage but a typed view: `root` is a property bag, and the compiler
 * wants chains. That is what this produces.
 *
 * It is a projection, not a second copy. Nothing here is serialized separately,
 * so a project file keeps one set of pads and no existing project changes shape.
 */

/// The pads saved for `pluginId` in `pluginState`, as a rack of chains.
///
/// Null when the device is not a pad-per-chain device, when it has no pads
/// saved, and when the state cannot be read. Reads both a v2 document and the
/// pre-v2 engine XML, because projects on disk still carry either.
std::unique_ptr<RackInfo> readPadRack(const juce::String& pluginId,
                                      const juce::String& pluginState);

/// Point `device.padRack` at whatever `device.pluginState` currently holds.
///
/// Clears it for a device with no pads, so a device that loses them does not
/// keep a stale set. Cheap enough to call on any device: it does no parsing at
/// all unless the id is one that has pads.
void refreshPadRack(DeviceInfo& device);

/// True when devices of this type keep their chains as pads.
bool isPadRackDevice(const juce::String& pluginId);

}  // namespace magda
