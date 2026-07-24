#pragma once

#include "core/ParameterInfo.hpp"

namespace magda {

inline std::shared_ptr<ParameterInfo::DisplayTextProvider> makeDeviceParameterDisplayTextProvider(
    const ChainNodePath& devicePath, int deviceId, int paramIndex) {
    return makeParameterDisplayTextProvider(devicePath, deviceId, paramIndex);
}

}  // namespace magda
