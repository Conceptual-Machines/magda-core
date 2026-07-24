#pragma once

#include "core/ParameterInfo.hpp"

namespace magda {

std::shared_ptr<ParameterInfo::DisplayTextProvider> makeDeviceParameterDisplayTextProvider(
    const ChainNodePath& devicePath, int deviceId, int paramIndex);

void installDeviceParameterDisplayTextProviderFactory();

}  // namespace magda
