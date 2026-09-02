#pragma once

#include "processors/base/DeviceProcessor.hpp"

namespace magda {

/**
 * @brief Processor for a MagdaDevice the host adapter wraps.
 *
 * The chain holds a TracktionMagdaDevicePlugin and its te parameters are
 * normalized [0, 1] slots, so the display metadata a plain
 * AutomatablePluginProcessor would read off those parameters is gone. This
 * processor reaches through the wrapper to the device's own ParameterInfo
 * instead, and the display<->normalized conversion happens here, in one
 * place. The model keeps seeing real display units and ranges - which is what
 * projects store and what the custom faceplates are written against - exactly
 * as CompiledFaustProcessor does for the compiled pack.
 *
 * The base for every hand-written device that crosses to MagdaDevice (#2299).
 */
class MagdaDeviceProcessor : public DeviceProcessor {
  public:
    MagdaDeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParametersFromEngine(DeviceInfo& info) const override;
    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

}  // namespace magda
