#pragma once

#include "processors/base/DeviceProcessor.hpp"

namespace magda {

/**
 * @brief Processor for external VST3/AU plugins
 *
 * Maps plugin parameters to DeviceInfo.parameters and handles
 * bidirectional sync between the UI and the plugin.
 *
 * Also listens for parameter changes from the plugin's native UI
 * and propagates them to TrackManager.
 */
class ExternalPluginProcessor : public DeviceProcessor, public te::AutomatableParameter::Listener {
  public:
    ExternalPluginProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
    ~ExternalPluginProcessor() override;

    void setParameter(const juce::String& paramName, float value) override;
    float getParameter(const juce::String& paramName) const override;
    std::vector<juce::String> getParameterNames() const override;
    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void syncFromDeviceInfo(const DeviceInfo& info) override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;

    void startParameterListening();
    void stopParameterListening();

    void curveHasChanged(te::AutomatableParameter&) override {}
    void currentValueChanged(te::AutomatableParameter& param) override;
    void parameterChanged(te::AutomatableParameter& param, float newValue) override;

  private:
    te::ExternalPlugin* getExternalPlugin() const;

    mutable std::vector<juce::String> parameterNames_;
    mutable bool parametersCached_ = false;
    bool listeningForChanges_ = false;
    bool settingParameterFromUI_ = false;

    void cacheParameterNames() const;
    void propagateParameterChange(te::AutomatableParameter& param);
};

}  // namespace magda
