#pragma once

#include <array>
#include <random>

#include "core/ParameterUtils.hpp"
#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

/**
 * @brief Test-tone generator: a single oscillator for calibration, routing
 *        checks and utility signals.
 *
 * MAGDA's own, replacing the stock Tracktion device it was hosting (#2192).
 * The parameter order, ids and ranges are the ones that device used, because
 * saved projects address them by index and the faceplate is written against
 * them: 0 = Waveform, 1 = Band Limit, 2 = Frequency, 3 = Level.
 *
 * This is a rewrite rather than a port -- there was no MAGDA DSP to move, only
 * a faceplate driving somebody else's oscillator -- so a project made before it
 * will sound very slightly different where the old device's band limiting
 * differed from the PolyBLEP used here. The waveforms, their order and their
 * ranges are unchanged.
 */
class ToneGeneratorPlugin : public MagdaDevice {
  public:
    ToneGeneratorPlugin();

    static const char* xmlTypeName;

    static const char* getPluginName() {
        return "Test Tone";
    }

    // The order is the retired device's, and saved projects address these by
    // index.
    static constexpr int kWaveformParamIndex = 0;
    static constexpr int kBandLimitParamIndex = 1;
    static constexpr int kFrequencyParamIndex = 2;
    static constexpr int kLevelParamIndex = 3;
    static constexpr int kParamCount = 4;

    enum class Waveform { Sine = 0, Triangle, SawUp, SawDown, Square, Noise };
    static constexpr int kWaveformCount = 6;

    DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = getPluginName(),
            .shortName = "Tone",
            .takesAudioInput = false,
            .producesAudioWithoutInput = true,
        };
    }

    void prepare(const DevicePrepareContext& context) override;
    void reset() override;
    void process(DeviceProcessContext& context) override;

    int parameterCount() const override {
        return kParamCount;
    }
    ParameterInfo parameterInfo(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

  private:
    float displayValue(int index) const;
    /// One sample of @p waveform at phase @p phase (0..1), band limited around
    /// its discontinuities when @p bandLimit is set.
    float oscillate(Waveform waveform, float phase, float phaseIncrement, bool bandLimit);

    std::array<float, kParamCount> values_{};
    std::array<ParameterUtils::ParameterDomain, kParamCount> domains_{};

    double sampleRate_ = 44100.0;
    float phase_ = 0.0f;
    std::minstd_rand noise_{0x5EED};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneGeneratorPlugin)
};

}  // namespace magda::daw::audio
