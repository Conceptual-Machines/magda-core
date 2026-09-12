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

}  // namespace magda
