#pragma once

#include "processors/base/DeviceProcessor.hpp"

namespace magda {

/**
 * @brief Processor for native plugins compiled from Faust DSP sources.
 *
 * These plugins expose host slots through ICompiledFaustPlugin. Their
 * AutomatableParameters are normalized at the TE boundary, while DeviceInfo
 * and automation lanes use display/native values.
 */
class CompiledFaustProcessor : public DeviceProcessor {
  public:
    CompiledFaustProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;
    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

}  // namespace magda
