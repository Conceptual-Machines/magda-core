#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "processors/base/DeviceProcessor.hpp"

namespace magda {

namespace te = tracktion;

namespace daw::audio {
class ArpeggiatorPlugin;
class StepSequencerPlugin;
}  // namespace daw::audio

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
 * @brief Processor for the built-in Magda Sampler device
 *
 * Sets parameters directly on the MagdaSamplerPlugin's automatable parameters by index.
 */
class MagdaSamplerProcessor : public DeviceProcessor {
  public:
    MagdaSamplerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

/**
 * @brief Processor for the built-in 4OSC synthesizer
 *
 * Enumerates parameters generically from plugin->getAutomatableParameters().
 * The UI maps each control to its param index.
 */
class FourOscProcessor : public DeviceProcessor {
  public:
    FourOscProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

/**
 * @brief Processor for the MAGDA-native Faust DSP host.
 *
 * Faust's parameters live in a fixed pool of 64 lifetime-stable
 * AutomatableParameters managed by FaustPlugin. ParameterInfo per
 * slot comes from `paramInfoFromSlot(slot)`; inactive slots return a
 * placeholder so paramIndex (== slot index) stays addressable for
 * automation lane lookups.
 */
class FaustProcessor : public DeviceProcessor {
  public:
    FaustProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
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

class ReverbProcessor : public DeviceProcessor {
  public:
    ReverbProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
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

class PitchShiftProcessor : public DeviceProcessor {
  public:
    PitchShiftProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
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

class ImpulseResponseProcessor : public DeviceProcessor {
  public:
    ImpulseResponseProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

/**
 * @brief Processor for the Arpeggiator plugin
 *
 * Exposes CachedValue-based parameters so macros can target them.
 * Parameters:
 * - 0: Pattern (discrete 0-5)
 * - 1: Rate (discrete 0-9)
 * - 2: Octaves (discrete 1-4)
 * - 3: Gate (0..1)
 * - 4: Swing (0..1)
 * - 5: Timing Depth (-1..1, ramp curve depth)
 * - 6: Timing Skew (-1..1, bipolar ramp curve skew)
 * - 7: Latch (0/1 boolean)
 * - 8: Velocity Mode (discrete 0-2)
 * - 9: Fixed Velocity (1-127)
 */
class ArpeggiatorProcessor : public DeviceProcessor {
  public:
    ArpeggiatorProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;

  private:
    daw::audio::ArpeggiatorPlugin* getArpPlugin() const;
};

/**
 * @brief Processor for the Step Sequencer plugin
 *
 * Exposes CachedValue-based parameters so macros can target them.
 * Parameters:
 * - 0: Rate (discrete 0-9)
 * - 1: Direction (discrete 0-3)
 * - 2: Swing (0..1)
 * - 3: Glide Time (0..1)
 * - 4: Accent Velocity (1-127)
 * - 5: Normal Velocity (1-127)
 */
class StepSequencerProcessor : public DeviceProcessor {
  public:
    StepSequencerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;

  private:
    daw::audio::StepSequencerPlugin* getSeqPlugin() const;
};

/**
 * @brief Processor for the built-in Drum Grid device
 *
 * Minimal processor — the drum grid has no top-level automatable params initially.
 * Per-pad parameters live on child plugins inside DrumGridPlugin.
 */
class DrumGridProcessor : public DeviceProcessor {
  public:
    DrumGridProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;
};

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

    /**
     * @brief Set a parameter by index (for UI sliders)
     * @param paramIndex Index into the plugin's automatable parameters
     * @param value Normalized value (0-1) or actual value depending on parameter type
     */
    void setParameterByIndex(int paramIndex, float value) override;

    /**
     * @brief Get a parameter value by index
     * @param paramIndex Index into the plugin's automatable parameters
     * @return Current value
     */
    float getParameterByIndex(int paramIndex) const;

    /**
     * @brief Start listening for parameter changes from the plugin's native UI
     * Call this after the plugin is fully loaded
     */
    void startParameterListening();

    /**
     * @brief Stop listening for parameter changes
     */
    void stopParameterListening();

    // te::AutomatableParameter::Listener interface
    void curveHasChanged(te::AutomatableParameter&) override {}
    void currentValueChanged(te::AutomatableParameter& param) override;
    void parameterChanged(te::AutomatableParameter& param, float newValue) override;

  private:
    te::ExternalPlugin* getExternalPlugin() const;

    // Cache parameter names for fast lookup
    mutable std::vector<juce::String> parameterNames_;
    mutable bool parametersCached_ = false;
    bool listeningForChanges_ = false;

    // Flag to prevent feedback loops when we're setting a parameter ourselves
    bool settingParameterFromUI_ = false;

    void cacheParameterNames() const;
    void propagateParameterChange(te::AutomatableParameter& param);
};

}  // namespace magda
