#pragma once

#include <vector>

#include "core/ChainNodePath.hpp"
#include "core/ParameterInfo.hpp"

/**
 * @file DeviceParameterList.hpp
 * @brief What a device's parameters are, for everything that shows them (#2634).
 *
 * A hosted plugin's list comes from the instance, which is the only thing that
 * can enumerate it; the model mirrors a value only for a slot something
 * addresses (#2629).
 */

namespace magda {

struct DeviceInfo;

/**
 * @brief The parameters of the device at @p devicePath, in list order.
 *
 * Positions are what the customization lists key on and `paramIndex` is the
 * slot a write addresses, exactly as DeviceInfo::parameters has them. Internal
 * devices and plugins the engine holds no instance for answer from the model.
 */
std::vector<ParameterInfo> deviceParameterList(const DeviceInfo& device,
                                               const ChainNodePath& devicePath);

}  // namespace magda
