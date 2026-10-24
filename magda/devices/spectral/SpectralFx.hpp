#pragma once

#include <array>
#include <magda/sdk/device/Device.hpp>
#include <magda/sdk/dsp/Random.hpp>
#include <magda/sdk/tap/SampleRing.hpp>
#include <vector>

#include "devices/spectral/SpectrogramTelemetry.hpp"
#include "devices/spectral/Stft.hpp"

namespace magda::devices::spectral {

/**
 * @brief Spectral effects in series on one STFT: gate, contrast, frequency shift, phase scramble
 *        (#1263). Clean-room from the published techniques dtblkfx popularised.
 */
class SpectralFx : public sdk::Device, public SpectrogramTelemetry {
  public:
    SpectralFx();

    static constexpr const char* xmlTypeName = "spectralfx";
    static constexpr const char* kName = "Spectral FX";

    static constexpr int kGateParamIndex = 0;
    static constexpr int kContrastParamIndex = 1;
    static constexpr int kShiftParamIndex = 2;
    static constexpr int kScrambleParamIndex = 3;
    static constexpr int kMixParamIndex = 4;
    static constexpr int kOutputParamIndex = 5;
    static constexpr int kParamCount = 6;

    /// The gate's floor: at or below it the gate is off.
    static constexpr float kGateOffDb = -80.0f;
    static constexpr int kMaxChannels = 8;

    sdk::DeviceProperties properties() const override {
        return {
            .pluginId = xmlTypeName,
            .name = kName,
            .shortName = "Spec",
        };
    }

    void prepare(const sdk::PrepareContext& context) override;
    void reset() override;
    int latencySamples() const override {
        return stft_.latencySamples();
    }
    void process(sdk::ProcessContext& context) override;

    int parameterCount() const override {
        return kParamCount;
    }
    sdk::ParameterDescriptor parameterDescriptor(int index) const override;
    float parameterValue(int index) const override;
    void setParameterValue(int index, float value) override;

    std::string_view telemetryKey() const override {
        return kKey;
    }
    sdk::DeviceTelemetry* telemetry(std::string_view key) override {
        return key == kKey ? this : nullptr;
    }
    const sdk::DeviceTelemetry* telemetry(std::string_view key) const override {
        return key == kKey ? this : nullptr;
    }
    std::size_t writePosition() const override {
        return spectrogram_.writePosition();
    }
    std::size_t readLatest(float* dest, int numSamples) const override {
        return spectrogram_.readLatest(dest, numSamples);
    }

  private:
    /// The per-frame settings, read once per block.
    struct FrameSettings {
        float gateThreshold = 0.0f;  // linear, 0 is off
        float contrastExponent = 1.0f;
        int shiftBins = 0;
        double shiftPhasePerFrame = 0.0;
        float scramble = 0.0f;
    };

    float displayValue(int index) const;
    FrameSettings frameSettings() const;
    void processFrame(int channel, float* spectrum, const FrameSettings& settings);
    void collectColumn(int channel, const float* spectrum);
    void publishColumns(int numChannels);

    std::array<float, kParamCount> values_{};
    std::array<sdk::ParameterDomain, kParamCount> domains_{};

    Stft stft_;
    double sampleRate_ = 44100.0;
    std::vector<float> magnitude_;
    std::vector<float> phase_;
    std::vector<float> shiftedMagnitude_;
    std::vector<float> shiftedPhase_;
    std::array<double, kMaxChannels> shiftPhase_{};
    float wetGain_ = 1.0f;  // last block's, ramped from
    float dryGain_ = 0.0f;
    sdk::Xoshiro256 random_{0x5EC7};

    /// Frames a block can hold past which its columns are dropped from the display.
    static constexpr int kMaxPendingColumns = 32;
    engine::SampleRing spectrogram_{kColumnBands * 512};
    std::array<int, kColumnBands> bandLow_{};
    std::array<int, kColumnBands> bandHigh_{};  // exclusive
    std::vector<float> pendingColumns_;         // kMaxPendingColumns x kColumnBands
    std::array<int, kMaxChannels> blockFrames_{};
};

}  // namespace magda::devices::spectral
