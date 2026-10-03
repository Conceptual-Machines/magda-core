#pragma once

#include <array>
#include <magda/sdk/device/Device.hpp>
#include <random>

namespace magda::devices {

/**
 * @brief Test-tone generator: a single oscillator for calibration, routing checks and utility
 *        signals. JUCE-free, so it also builds for the ABI hosts (#2940).
 *
 * MAGDA's own (#2192). The parameter order, ids and ranges are those of the earlier stock device,
 * because saved projects address them by index: 0 = Waveform, 1 = Band Limit, 2 = Frequency,
 * 3 = Level.
 */
class ToneGenerator : public sdk::Device {
  public:
    ToneGenerator();

    static constexpr const char* kDeviceType = "toneGenerator";
    static constexpr const char* kName = "Test Tone";

    static constexpr int kWaveformParamIndex = 0;
    static constexpr int kBandLimitParamIndex = 1;
    static constexpr int kFrequencyParamIndex = 2;
    static constexpr int kLevelParamIndex = 3;
    static constexpr int kParamCount = 4;

    enum class Waveform { Sine = 0, Triangle, SawUp, SawDown, Square, Noise };
    static constexpr int kWaveformCount = 6;

    sdk::DeviceProperties properties() const override {
        return {
            .pluginId = kDeviceType,
            .name = kName,
            .shortName = "Tone",
            .takesAudioInput = false,
            .producesAudioWithoutInput = true,
        };
    }

    void prepare(const sdk::PrepareContext& context) override;
    void reset() override;
    void process(sdk::ProcessContext& context) override;

    int parameterCount() const override {
        return kParamCount;
    }
    sdk::ParameterDescriptor parameterDescriptor(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

  private:
    float displayValue(int index) const;
    /// One sample of @p waveform at phase @p phase (0..1), band limited around its
    /// discontinuities when @p bandLimit is set.
    float oscillate(Waveform waveform, float phase, float phaseIncrement, bool bandLimit);

    std::array<float, kParamCount> values_{};
    std::array<sdk::ParameterDomain, kParamCount> domains_{};

    double sampleRate_ = 44100.0;
    float phase_ = 0.0f;
    std::minstd_rand noise_{0x5EED};
};

}  // namespace magda::devices
