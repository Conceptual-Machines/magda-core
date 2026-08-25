#pragma once

#include <atomic>
#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

namespace magda::daw::audio::compiled {

class MagdaLimiterDspCore {
  public:
    struct Settings {
        float thresholdDb = -1.0f;
        float attackMs = 1.0f;
        float releaseMs = 200.0f;
        float outputDb = 0.0f;
    };

    struct Stats {
        float inputPeak = 0.0f;
        float outputPeak = 0.0f;
        float gainReductionDb = 0.0f;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    Stats process(juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                  const Settings& settings);

  private:
    static float dbToGain(float db);
    static float coefficient(float timeMs, double sampleRate);

    double sampleRate_ = 44100.0;
    int delaySamples_ = 1;
    int writeIndex_ = 0;
    float gain_ = 1.0f;

    std::vector<std::vector<float>> delayLines_;
    std::vector<float> frame_;
};

/**
 * @brief Native lookahead limiter / autonormalizer.
 *
 * Threshold is baked-in normalizer drive into a fixed 0 dB limiter ceiling.
 * Output is a post-limiter trim and is restricted to negative gain, so it can
 * only reduce the emitted level after limiting.
 */
class MagdaLimiterCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaLimiterCompiledPlugin();

    static constexpr int kThresholdSlot = 0;
    static constexpr int kAttackSlot = 1;
    static constexpr int kReleaseSlot = 2;
    static constexpr int kOutputSlot = 3;
    static constexpr int kHostSlotCount = 4;

    // Audio-thread metering taps, read by the curve view on its timer.
    float getInputPeakDb() const {
        return inputPeakDb_.load(std::memory_order_relaxed);
    }
    float getOutputPeakDb() const {
        return outputPeakDb_.load(std::memory_order_relaxed);
    }
    float getGainReductionDb() const {
        return gainReductionDb_.load(std::memory_order_relaxed);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Limiter";
    }

  protected:
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_limiter_";
    }
    void onPrepare(double sampleRate, int maximumBlockSize) override;
    void onRelease() override;
    void onReset() override;
    void processAudio(DeviceProcessContext& context) override;

  private:
    MagdaLimiterDspCore limiter_;

    std::atomic<float> inputPeakDb_{-120.0f};
    std::atomic<float> outputPeakDb_{-120.0f};
    std::atomic<float> gainReductionDb_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaLimiterCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
