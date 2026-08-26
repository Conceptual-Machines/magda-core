#pragma once

namespace magda {
struct DeviceInfo;
}

namespace magda::daw::audio {

class DrumGridPlugin;

/**
 * Fill the parameters of the devices projected from @p plugin's pads (#2200).
 *
 * The pads reach the model as a projection of the device's saved state, which
 * carries a plugin's parameter values but not its names, ranges or count: only
 * the plugin answers those. Without them the parameter table allocates no window
 * for a pad's device, and a native-capable plugin inside a pad runs at its
 * defaults rather than at what the project saved, because the engine's factory
 * restores everything EXCEPT parameters and expects the table to supply them.
 *
 * Called where the pads are projected from a live device, which is the same
 * point their DeviceIds and instrument flags arrive at.
 */
void populatePadDeviceParameters(DeviceInfo& device, DrumGridPlugin& plugin);

}  // namespace magda::daw::audio
