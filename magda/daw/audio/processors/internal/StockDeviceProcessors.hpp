#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "processors/base/AutomatablePluginProcessor.hpp"

namespace magda::daw::audio {
class ToneGeneratorPlugin;
}

namespace magda {

namespace te = tracktion;

/**
 * Processors for the stock Tracktion devices MAGDA still ships.
 *
 * What is left here is the tail of the Tracktion device set: one device still
 * offered in the browser (Test Tone) and the hidden Volume/Pan kept for old
 * project loads. The rest (EQ, Compressor, Delay, Chorus, Phaser, Reverb,
 * Pitch Shift, Lowpass, IR Reverb) are gone, and load as their MAGDA
 * successors instead (see core/LegacyDeviceAliases.hpp). 4OSC keeps its own
 * processor in NativeDeviceProcessors because its custom UI reads
 * non-automatable state.
 *
 * Each of these is waiting on a MAGDA-native replacement; when one lands, its
 * processor leaves this file, and the file goes with the last of them.
 */

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
    daw::audio::ToneGeneratorPlugin* getToneDevice() const;
    void setSlotDisplayValue(int slot, float displayValue) const;
    float slotDisplayValue(int slot, float fallback) const;
    bool initialized_ = false;
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
    void populateParametersFromEngine(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;

  private:
    te::VolumeAndPanPlugin* getVolPanPlugin() const;
};

}  // namespace magda
