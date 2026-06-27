#pragma once

#include "processors/base/AutomatablePluginProcessor.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Processor for the built-in Magda Sampler device
 *
 * Sets parameters directly on the MagdaSamplerPlugin's automatable parameters by index.
 */
class MagdaSamplerProcessor : public AutomatablePluginProcessor {
  public:
    MagdaSamplerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the native Mutable Instruments Elements synth.
 *
 * Parameters are addressed by index off the plugin's automatable parameters.
 */
class MutableElementsProcessor : public AutomatablePluginProcessor {
  public:
    MutableElementsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the native Mutable Instruments Rings resonator.
 */
class MutableRingsProcessor : public AutomatablePluginProcessor {
  public:
    MutableRingsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the native Mutable Instruments Clouds granular FX.
 */
class MutableCloudsProcessor : public AutomatablePluginProcessor {
  public:
    MutableCloudsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the built-in 4OSC synthesizer
 *
 * Enumerates parameters generically from plugin->getAutomatableParameters().
 * The UI maps each control to its param index.
 */
class FourOscProcessor : public AutomatablePluginProcessor {
  public:
    FourOscProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

  protected:
    void customiseParameterInfo(int index, ParameterInfo& info) const override;
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
 * @brief Processor for the MAGDA-native Faust polyphonic instrument.
 *
 * Identical pool-backed parameter model to FaustProcessor, but bound to
 * FaustInstrumentPlugin (the synth sibling of the Faust effect host).
 */
class FaustInstrumentProcessor : public DeviceProcessor {
  public:
    FaustInstrumentProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);

    int getParameterCount() const override;
    ParameterInfo getParameterInfo(int index) const override;
    void populateParameters(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

}  // namespace magda
