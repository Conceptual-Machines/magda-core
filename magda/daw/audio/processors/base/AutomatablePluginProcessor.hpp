#pragma once

#include "processors/base/DeviceProcessor.hpp"

namespace magda {

class AutomatablePluginProcessor : public DeviceProcessor {
  public:
    using DeviceProcessor::DeviceProcessor;

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;

  protected:
    virtual void customiseParameterInfo(int index, ParameterInfo& info) const;

    juce::Array<te::AutomatableParameter*> getAutomatableParameters() const;
};

}  // namespace magda
