#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "processors/base/AutomatablePluginProcessor.hpp"

namespace magda {

namespace te = tracktion;

// =============================================================================
// Specialized Processors
// =============================================================================

/**
 * @brief Processor for the built-in Tone Generator device
 *
 * Parameter indexing matches TE's ToneGeneratorPlugin::getAutomatableParameters()
 * so that mod/macro links resolve to the correct TE parameter:
 * - 0: oscType  (discrete, 0=Sine .. 5=Noise — matches te::ToneGeneratorPlugin::OscType)
 * - 1: bandLimit (discrete, 0=Aliased, 1=Band Limited — not shown in MAGDA UI)
 * - 2: frequency (Hz, 20-20000, logarithmic)
 * - 3: level     (dB in UI, linear 0-1 on the plugin)
 */
class ToneGeneratorProcessor : public DeviceProcessor {
  public:
    ToneGeneratorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    void setParameter(const juce::String& paramName, float value) override;
    float getParameter(const juce::String& paramName) const override;
    std::vector<juce::String> getParameterNames() const override;
    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;

    // Single-parameter sync from DeviceInfo (values in real units: TE osc enum / bandLimit / Hz /
    // dB)
    void setParameterByIndex(int paramIndex, float value) override;

    // Initialize with default values - call after processor is fully set up
    void initializeDefaults();

    // Convenience methods
    void setFrequency(float hz);
    float getFrequency() const;

    void setLevel(float level);  // 0-1 linear
    float getLevel() const;

    void setOscType(int teOscType);  // TE enum: 0=sin,1=triangle,2=sawUp,3=sawDown,4=square,5=noise
    int getOscType() const;

    void setBandLimit(bool bandLimited);
    bool getBandLimit() const;

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

    void setParameter(const juce::String& paramName, float value) override;
    float getParameter(const juce::String& paramName) const override;
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

/**
 * @brief Processor for the built-in 4-Band Equaliser
 *
 * Enumerates parameters generically from plugin->getAutomatableParameters().
 * Parameter order: loFreq, loGain, loQ, midFreq1, midGain1, midQ1,
 *                  midFreq2, midGain2, midQ2, hiFreq, hiGain, hiQ
 */
class EqualiserProcessor : public DeviceProcessor {
  public:
    EqualiserProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

class CompressorProcessor : public DeviceProcessor {
  public:
    CompressorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

class DelayProcessor : public DeviceProcessor {
  public:
    DelayProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

class ReverbProcessor : public AutomatablePluginProcessor {
  public:
    ReverbProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

class ChorusProcessor : public DeviceProcessor {
  public:
    ChorusProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

class PhaserProcessor : public DeviceProcessor {
  public:
    PhaserProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

class FilterProcessor : public DeviceProcessor {
  public:
    FilterProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

class PitchShiftProcessor : public AutomatablePluginProcessor {
  public:
    PitchShiftProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the Utility plugin (gain, pan, phase inversion)
 *
 * Parameters:
 * - 0: Volume (slider position 0..1, displayed as dB)
 * - 1: Pan (-1..1)
 * - 2: Polarity (0/1, virtual — CachedValue<bool>, not automatable)
 */
class UtilityProcessor : public DeviceProcessor {
  public:
    UtilityProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;

  private:
    te::VolumeAndPanPlugin* getVolPanPlugin() const;
};

class ImpulseResponseProcessor : public AutomatablePluginProcessor {
  public:
    ImpulseResponseProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

}  // namespace magda
