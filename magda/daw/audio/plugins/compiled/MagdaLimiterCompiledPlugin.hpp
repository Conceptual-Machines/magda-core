#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust stereo lookahead brickwall limiter.
 *
 * Sanfilippo design from co.limiter_lad_stereo — peak-holder feeding
 * tau-smoothed attack/release with a fixed 5 ms lookahead delay. Used to
 * guard against hard-clipping with minimal coloration. Threshold is the
 * output ceiling in dB; signal above it is attenuated, signal below it
 * passes through unchanged.
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

    // Slot ordering matches [idx:N] pins inside magda_limiter.dsp.
    static constexpr int kThresholdSlot = 0;
    static constexpr int kAttackSlot = 1;
    static constexpr int kHoldSlot = 2;
    static constexpr int kReleaseSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kAutogainSlot = 6;
    static constexpr int kHostSlotCount = 7;

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
    void rebuildEngineState(int sampleRate);

    std::unique_ptr<::dsp> dsp_;
    int numInputs_ = 0;
    int numOutputs_ = 0;

    std::array<FAUSTFLOAT*, kHostSlotCount> zones_{};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    std::atomic<float> inputPeakDb_{-120.0f};
    std::atomic<float> outputPeakDb_{-120.0f};
    std::atomic<float> gainReductionDb_{0.0f};

    uint32_t debugTraceCounter_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaLimiterCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
