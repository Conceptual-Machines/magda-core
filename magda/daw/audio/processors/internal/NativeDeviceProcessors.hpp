#pragma once

#include <optional>

#include "processors/base/AutomatablePluginProcessor.hpp"
#include "processors/base/MagdaDeviceProcessor.hpp"

namespace magda {

namespace te = tracktion;

/// 4OSC's non-automatable CachedValues, captured from the live plugin for the custom UI.
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

/// Processor for the built-in Magda Sampler, a MagdaDevice (#2271).
class MagdaSamplerProcessor : public MagdaDeviceProcessor {
  public:
    MagdaSamplerProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/// Processor for the native Mutable Instruments Elements synth, a MagdaDevice (#2299).
class MutableElementsProcessor : public MagdaDeviceProcessor {
  public:
    MutableElementsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/// Processor for the native Mutable Instruments Rings resonator, a MagdaDevice (#2299).
class MutableRingsProcessor : public MagdaDeviceProcessor {
  public:
    MutableRingsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/// Processor for the native Mutable Instruments Clouds granular FX, a MagdaDevice (#2299).
class MutableCloudsProcessor : public MagdaDeviceProcessor {
  public:
    MutableCloudsProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/// Processor for the native convolution device, a MagdaDevice (#2299). The
/// impulse response is device state, loaded via the `impulseResponseLoadFile` command.
class MagdaConvolutionProcessor : public MagdaDeviceProcessor {
  public:
    MagdaConvolutionProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/// Processor for the Sidechain volume-shaper. Reads through the device, not the
/// wrapper's normalised slots: attack and release are milliseconds (#2613).
class SidechainProcessor : public MagdaDeviceProcessor {
  public:
    SidechainProcessor(DeviceId deviceId, te::Plugin::Ptr plugin);
};

/// Processor for the built-in 4OSC synthesizer.
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
 * Parameters are FaustPlugin's fixed pool of 64 stable slots; paramIndex is the
 * slot index, and an inactive slot returns a placeholder so it stays addressable.
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

/// Processor for the MAGDA-native Faust polyphonic instrument. Same pool model
/// as FaustProcessor, bound to FaustInstrumentPlugin.
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
