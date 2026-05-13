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
 * @brief Compiled-Faust stereo flanger.
 *
 * Single short modulated delay (≈3 ms ± 2.5 ms) per channel with a
 * heavy feedback loop — the classic comb-sweep character. Rate is
 * either free (Hz) or tempo-synced via the shared musical-division
 * menu. Width spreads the L/R LFO phase offset.
 */
class MagdaFlangerCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaFlangerCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaFlangerCompiledPlugin() override;

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
        return 0.05;
    }

    // Slot ordering matches [idx:N] pins inside magda_flanger.dsp.
    static constexpr int kSyncSlot = 0;
    static constexpr int kRateSlot = 1;
    static constexpr int kDivisionSlot = 2;
    static constexpr int kDepthSlot = 3;
    static constexpr int kFeedbackSlot = 4;
    static constexpr int kMixSlot = 5;
    static constexpr int kWidthSlot = 6;
    static constexpr int kHostSlotCount = 7;
    static constexpr int kBpmSlot = 63;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    float divisionFaustValueForIndex(int index) const;

    float currentBpm() const {
        return currentBpm_.load(std::memory_order_relaxed);
    }

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
    FAUSTFLOAT* bpmZone_ = nullptr;
    std::vector<float> divisionChoiceValues_;
    std::vector<juce::String> divisionChoiceLabels_;

    struct GateSpec {
        int slotIndex = -1;
        bool negated = false;
    };
    std::array<GateSpec, kHostSlotCount> harvestedGates_{};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    std::atomic<float> currentBpm_{120.0f};
    bool wasPlaying_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFlangerCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
