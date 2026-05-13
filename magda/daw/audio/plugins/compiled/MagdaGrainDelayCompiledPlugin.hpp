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
 * @brief Compiled-Faust stereo granular delay.
 *
 * Same host contract as the compiled delay: controls are pinned by [idx:N],
 * the hidden BPM slot is host-written every block, and the Division menu
 * stores a display index while audio receives the Faust-side division value.
 */
class MagdaGrainDelayCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaGrainDelayCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaGrainDelayCompiledPlugin() override;

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
        return true;
    }
    double getTailLength() const override {
        return 4.0;
    }

    static constexpr int kTimeSlot = 0;
    static constexpr int kDivisionSlot = 1;
    static constexpr int kSyncSlot = 2;
    static constexpr int kSizeSlot = 3;
    static constexpr int kPitchSlot = 4;
    static constexpr int kSpraySlot = 5;
    static constexpr int kFeedbackSlot = 6;
    static constexpr int kMixSlot = 7;
    static constexpr int kHostSlotCount = 8;
    static constexpr int kBpmSlot = 63;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    float divisionFaustValueForIndex(int index) const;

    // Last BPM read from the edit's tempo sequence in applyToBuffer. See
    // MagdaDelayCompiledPlugin::currentBpm() — same contract.
    float currentBpm() const {
        return currentBpm_.load(std::memory_order_relaxed);
    }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaGrainDelayCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
