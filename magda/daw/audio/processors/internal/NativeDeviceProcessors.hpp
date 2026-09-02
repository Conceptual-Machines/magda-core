#pragma once

#include <optional>

#include "processors/base/AutomatablePluginProcessor.hpp"
#include "processors/base/MagdaDeviceProcessor.hpp"

namespace magda {

namespace te = tracktion;

/**
 * @brief Non-automatable plugin state for the 4OSC synth.
 *
 * These are CachedValues that are not exposed as AutomatableParameters, so
 * the FourOscProcessor captures them from the live plugin for the custom UI.
 */
struct FourOscPluginState {
    int oscWaveShape[4] = {0, 0, 0, 0};
    int oscVoices[4] = {1, 1, 1, 1};
    int filterType = 0;
    int filterSlope = 0;
    bool ampAnalog = false;
    int lfoWaveShape[2] = {0, 0};
    bool lfoSync[2] = {false, false};
    bool distortionOn = false;
    bool reverbOn = false;
    bool delayOn = false;
    bool chorusOn = false;
    int voiceMode = 2;      // 0=Mono, 1=Legato, 2=Poly
    int globalVoices = 32;  // Max polyphony
};

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
 * Elements is a MagdaDevice (#2299), so the chain holds the host's wrapper
 * and the display metadata comes from the device's own slots.
 */
class MutableElementsProcessor : public MagdaDeviceProcessor {
  public:
    MutableElementsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the native Mutable Instruments Rings resonator.
 *
 * Rings is a MagdaDevice (#2299), so the chain holds the host's wrapper and
 * the display metadata comes from the device's own slots.
 */
class MutableRingsProcessor : public MagdaDeviceProcessor {
  public:
    MutableRingsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the native Mutable Instruments Clouds granular FX.
 *
 * Clouds is a MagdaDevice (#2299), so the chain holds the host's wrapper and
 * the display metadata comes from the device's own slots.
 */
class MutableCloudsProcessor : public MagdaDeviceProcessor {
  public:
    MutableCloudsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the native convolution device (IR Reverb).
 *
 * The device is a MagdaDevice (#2299): gain, low cut, high cut, mix and
 * filter Q come from its slots. The impulse response itself is not a
 * parameter - it lives in the device's state and is loaded through the
 * `impulseResponseLoadFile` device command.
 */
class MagdaConvolutionProcessor : public MagdaDeviceProcessor {
  public:
    MagdaConvolutionProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/**
 * @brief Processor for the Sidechain volume-shaper insert.
 *
 * Parameters (gain / attack / release) are addressed by index off the
 * plugin's automatable parameters.
 */
class SidechainProcessor : public AutomatablePluginProcessor {
  public:
    SidechainProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
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

    static std::optional<FourOscPluginState> capturePluginState(te::Plugin* plugin);

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
    void populateParametersFromEngine(DeviceInfo& info) const override;

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
    void populateParametersFromEngine(DeviceInfo& info) const override;

    void setParameterByIndex(int paramIndex, float value) override;
    float getParameterByIndex(int paramIndex) const;
};

}  // namespace magda
