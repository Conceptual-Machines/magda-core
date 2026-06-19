#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <vector>

#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

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
class MagdaLimiterCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaLimiterCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaLimiterCompiledPlugin() override;

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

    void initialise(const te::PluginInitialisationInfo& info) override;
    void deinitialise() override;
    void reset() override;
    void applyToBuffer(const te::PluginRenderContext& fc) override;

    bool takesMidiInput() override {
        return false;
    }
    bool takesAudioInput() override {
        return true;
    }
    bool isSynth() override {
        return false;
    }
    bool producesAudioWhenNoAudioInput() override {
        return false;
    }
    double getTailLength() const override {
        return 0.0;
    }

    static constexpr int kThresholdSlot = 0;
    static constexpr int kAttackSlot = 1;
    static constexpr int kReleaseSlot = 2;
    static constexpr int kOutputSlot = 3;
    static constexpr int kHostSlotCount = 4;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    // Audio-thread metering taps, read by the curve view via 33 ms timer.
    float getInputPeakDb() const {
        return inputPeakDb_.load(std::memory_order_relaxed);
    }
    float getOutputPeakDb() const {
        return outputPeakDb_.load(std::memory_order_relaxed);
    }
    float getGainReductionDb() const {
        return gainReductionDb_.load(std::memory_order_relaxed);
    }

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    // ICompiledFaustPlugin
    int hostSlotCount() const override {
        return kHostSlotCount;
    }
    const CompiledHostSlotInfo& hostSlotInfo(int slotIndex) const override {
        return getSlotInfo(slotIndex);
    }
    te::AutomatableParameter* hostSlotParameter(int slotIndex) const override {
        return getSlotParameter(slotIndex);
    }
    float displayToNormalized(int slotIndex, float displayValue) const override {
        return displayValueToNativeValue(slotIndex, displayValue);
    }
    float normalizedToDisplay(int slotIndex, float normalizedValue) const override {
        return nativeValueToDisplayValue(slotIndex, normalizedValue);
    }

  private:
    void buildHostParameters();

    MagdaLimiterDspCore limiter_;

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    std::atomic<float> inputPeakDb_{-120.0f};
    std::atomic<float> outputPeakDb_{-120.0f};
    std::atomic<float> gainReductionDb_{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaLimiterCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
