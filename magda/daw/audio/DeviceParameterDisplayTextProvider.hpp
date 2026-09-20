#pragma once

#include <juce_core/juce_core.h>

#include <functional>

#include "core/ChainNodePath.hpp"

namespace magda {

struct DeviceInfo;

void installDeviceParameterDisplayTextProviderFactory();

/**
 * @brief How a device's parameter value is named, from whichever engine renders it.
 *
 * Only the instance that renders holds the plugin that can name a value, and under the
 * native engine there is no fork bridge at all (#2600), so the engine registers this and
 * an unformatted value falls back to its range.
 */
using DeviceParameterFormatter =
    std::function<juce::String(const ChainNodePath&, int paramIndex, float normalised)>;

void setDeviceParameterFormatter(DeviceParameterFormatter formatter);
void forgetDeviceParameterFormatter();

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
