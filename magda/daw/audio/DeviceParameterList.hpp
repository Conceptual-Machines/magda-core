#pragma once

#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/ParameterInfo.hpp"

/**
 * @file DeviceParameterList.hpp
 * @brief What a device's parameters are, for everything that shows them (#2634).
 *
 * Only a hosted plugin's instance can enumerate its parameters (#2629).
 */

namespace magda {

struct DeviceInfo;

/**
 * @brief The parameters of the device at @p devicePath, in list order.
 *
 * Shaped like DeviceInfo::parameters: `paramIndex` is the slot a write
 * addresses. Answered from the model for an internal device, and for a plugin
 * the engine holds no instance for.
 */
std::vector<ParameterInfo> deviceParameterList(const DeviceInfo& device,
                                               const ChainNodePath& devicePath);

/**
 * @brief @p described at the values @p device holds for the slots it carries.
 *
 * A write reaches the model at once and the plugin a block later, so the model
 * is what a reader is shown for a slot it mirrors. The rest keep the values the
 * instance reported.
 */
std::vector<ParameterInfo> withModelValues(std::vector<ParameterInfo> described,
                                           const DeviceInfo& device);

}  // namespace magda
