#pragma once

#include "core/ChainNodePath.hpp"

namespace magda {

struct DeviceInfo;

void installDeviceParameterDisplayTextProviderFactory();

/**
 * @brief Give every parameter of @p device a live text provider (#2600).
 *
 * For a hosted plugin, whose parameters are normalised and so have nothing a
 * formatter could work from: the plugin is asked for its own string per value,
 * rather than sampled into a table that is a guess between its samples.
 *
 * @p devicePath is stored in each provider, so formatting later never falls
 * back to looking the device up by id -- an id names up to three devices, one
 * per chain section (#1899), and the wrong one would display another plugin's
 * text. An invalid path leaves the parameters without providers, which formats
 * from the range instead.
 */
void attachParameterTextProviders(DeviceInfo& device, const ChainNodePath& devicePath);

}  // namespace magda
