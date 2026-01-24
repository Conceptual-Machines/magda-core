#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "../core/DeviceInfo.hpp"
#include "../core/TypeIds.hpp"

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

    // =========================================================================
    // Identification
    // =========================================================================

    DeviceId getDeviceId() const {
        return deviceId_;
    }
    te::Plugin::Ptr getPlugin() const {
        return plugin_;
    }

    // =========================================================================
    // Parameter Control
    // =========================================================================

    /**
     * @brief Set a named parameter on the device
     * @param paramName Parameter name (device-specific, e.g., "level", "frequency")
     * @param value Normalized value (0-1) or actual value depending on parameter
     * @param isNormalized If true, value is 0-1 normalized; if false, it's the actual value
     */
    virtual void setParameter(const juce::String& paramName, float value, bool isNormalized = true);

    /**
     * @brief Get a named parameter value
     * @param paramName Parameter name
     * @param normalized If true, return normalized 0-1 value
     * @return Parameter value, or 0 if not found
     */
    virtual float getParameter(const juce::String& paramName, bool normalized = true) const;

    /**
     * @brief Get list of available parameter names for this device
     */
    virtual std::vector<juce::String> getParameterNames() const;

    /**
     * @brief Get the number of parameters this device exposes
     */
    virtual int getParameterCount() const;

    /**
     * @brief Get parameter info for populating DeviceInfo
     * @param index Parameter index
     * @return ParameterInfo struct with name, range, value, etc.
     */
    virtual ParameterInfo getParameterInfo(int index) const;

    /**
     * @brief Populate DeviceInfo.parameters with current parameter state
     */
    virtual void populateParameters(DeviceInfo& info) const;

    // =========================================================================
    // Gain Stage
    // =========================================================================

    /**
     * @brief Set the device gain in dB
     * @param gainDb Gain in decibels (-inf to +12 typical range)
     */
    void setGainDb(float gainDb);

    /**
     * @brief Get the current gain in dB
     */
    float getGainDb() const {
        return gainDb_;
    }

    /**
     * @brief Set the device gain as linear value
     * @param gainLinear Linear gain (0 to ~4 for +12dB)
     */
    void setGainLinear(float gainLinear);

    /**
     * @brief Get the current gain as linear value
     */
    float getGainLinear() const {
        return gainLinear_;
    }

    // =========================================================================
    // Bypass
    // =========================================================================

    void setBypassed(bool bypassed);
    bool isBypassed() const;

    // =========================================================================
    // Sync with DeviceInfo
    // =========================================================================

    /**
     * @brief Update processor state from DeviceInfo
     * Call this when DeviceInfo changes
     */
    virtual void syncFromDeviceInfo(const DeviceInfo& info);

    /**
     * @brief Update DeviceInfo from processor state
     * Call this to persist changes back to the model
     */
    virtual void syncToDeviceInfo(DeviceInfo& info) const;

  protected:
    DeviceId deviceId_;
    te::Plugin::Ptr plugin_;

    // Gain stage state
    float gainDb_ = 0.0f;
    float gainLinear_ = 1.0f;

    // Apply gain to the appropriate plugin parameter
    virtual void applyGain();
};

// =============================================================================
// Specialized Processors
// =============================================================================

/**
 * @brief Processor for the built-in Tone Generator device
 *
 * Parameters:
 * - frequency: Tone frequency in Hz (20-20000)
 * - level: Output level (0-1 linear, maps to amplitude)
 * - oscType: Oscillator type (0=sine, 1=square, 2=saw, 3=noise)
 */
class ToneGeneratorProcessor : public DeviceProcessor {
  public:
    ToneGeneratorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    void setParameter(const juce::String& paramName, float value,
                      bool isNormalized = true) override;
    float getParameter(const juce::String& paramName, bool normalized = true) const override;
    std::vector<juce::String> getParameterNames() const override;
    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;

    // Initialize with default values - call after processor is fully set up
    void initializeDefaults();

    // Convenience methods
    void setFrequency(float hz);
    float getFrequency() const;

    void setLevel(float level);  // 0-1 linear
    float getLevel() const;

    void setOscType(int type);  // 0=sine, 1=noise
    int getOscType() const;

  protected:
    void applyGain() override;

  private:
    te::ToneGeneratorPlugin* getTonePlugin() const;
    bool initialized_ = false;
};

/**
 * @brief Processor for Volume & Pan (utility device)
 */
class VolumeProcessor : public DeviceProcessor {
  public:
    VolumeProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    void setParameter(const juce::String& paramName, float value,
                      bool isNormalized = true) override;
    float getParameter(const juce::String& paramName, bool normalized = true) const override;
    std::vector<juce::String> getParameterNames() const override;

    void setVolume(float db);
    float getVolume() const;

    void setPan(float pan);  // -1 to 1
    float getPan() const;

  protected:
    void applyGain() override;

  private:
    te::VolumeAndPanPlugin* getVolPanPlugin() const;
};

}  // namespace magda
