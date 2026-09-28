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
    /**
     * Where an internal device's parameter VALUES come from when `info` is
     * (re)populated. Mandatory at every call site, because which way values
     * flow is exactly the decision #2317 exists to make explicit.
     */
    enum class ValueSource {
        /// Values are read off the live plugin along with the metadata. For
        /// seeding a brand-new device, and the only correct source for an
        /// external plugin whose chunk is authoritative.
        Engine,
        /// The model is the authority (#2317): an entry `info` already carried
        /// keeps its `currentValue` when the refreshed entry still names the
        /// same parameter (same frozen index, same identity); only entries the
        /// model lacked - a new device, a parameter added by an update, a
        /// Faust recompile that changed what a slot means - take the engine's
        /// value. An EXTERNAL device passing through here still reads Engine:
        /// its chunk is authoritative and the model array mirrors it.
        Model
    };

    /**
     * Refresh `info`'s parameter list (metadata and values) from this
     * processor, with `source` deciding whether the engine's values may
     * overwrite the model's. The per-processor engine read is the protected
     * `populateParametersFromEngine()` hook.
     */
    void populateParameters(DeviceInfo& info, ValueSource source) const;
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
    void setDeltaSolo(bool deltaSolo);
    bool isDeltaSolo() const;

    virtual void syncFromDeviceInfo(const DeviceInfo& info);
    virtual void syncToDeviceInfo(DeviceInfo& info) const;

  protected:
    /// Read the full parameter list - metadata AND current values - off the
    /// live plugin into `info.parameters`. Never call directly from outside:
    /// the public populateParameters() applies the ValueSource policy on top.
    virtual void populateParametersFromEngine(DeviceInfo& info) const;

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
