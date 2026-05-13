#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../../../core/DeviceInfo.hpp"
#include "../../../core/TypeIds.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Processes a single device, bridging DeviceInfo state to plugin parameters
 *
 * Responsibilities:
 * - Apply gain stage from DeviceInfo
 * - Map device parameters to plugin parameters
 * - Handle bypass state
 * - Receive modulation values and apply to parameters
 *
 * Each DeviceProcessor is associated with one DeviceInfo and one Tracktion Plugin.
 */
class DeviceProcessor {
  public:
    DeviceProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
    virtual ~DeviceProcessor() = default;

    DeviceId getDeviceId() const {
        return deviceId_;
    }
    te::Plugin::Ptr getPlugin() const {
        return plugin_;
    }

    virtual void setParameter(const juce::String& paramName, float value);
    virtual float getParameter(const juce::String& paramName) const;
    virtual std::vector<juce::String> getParameterNames() const;
    virtual int getParameterCount() const;
    virtual ParameterInfo getParameterInfo(int index) const;
    virtual juce::String formatParameterValue(int index, float normalizedValue) const;
    virtual void populateParameters(DeviceInfo& info) const;
    virtual void setParameterByIndex(int paramIndex, float value);
    void setParameterByIndex(int paramIndex, ParameterModelValue value);

    void setGainDb(float gainDb);
    float getGainDb() const {
        return gainDb_;
    }

    void setGainLinear(float gainLinear);
    float getGainLinear() const {
        return gainLinear_;
    }

    void setBypassed(bool bypassed);
    bool isBypassed() const;

    virtual void syncFromDeviceInfo(const DeviceInfo& info);
    virtual void syncToDeviceInfo(DeviceInfo& info) const;

  protected:
    DeviceId deviceId_;
    te::Plugin::Ptr plugin_;

    float gainDb_ = 0.0f;
    float gainLinear_ = 1.0f;

    virtual void applyGain();

  private:
    mutable juce::Array<te::AutomatableParameter*> cachedParams_;
    mutable const te::Plugin* cachedParamsPlugin_ = nullptr;
};

}  // namespace magda
