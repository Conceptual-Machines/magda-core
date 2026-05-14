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
 * @brief Compiled-Faust stereo ring modulator.
 *
 * Multiplies the input by a sine / triangle / square carrier in the
 * 1 Hz – 5 kHz range. Low frequencies act like a tremolo; audio-rate
 * frequencies give the classic metallic-clang ring-mod timbre.
 */
class MagdaRingModCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaRingModCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaRingModCompiledPlugin() override;

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
    bool canSidechain() override {
        return true;  // Source=Sidechain uses input channel 3 as the carrier.
    }
    double getTailLength() const override {
        return 0.0;
    }

    // Slot ordering matches [idx:N] pins inside magda_ring_mod.dsp.
    static constexpr int kSyncSlot = 0;
    static constexpr int kFrequencySlot = 1;
    static constexpr int kDivisionSlot = 2;
    static constexpr int kShapeSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kWidthSlot = 5;
    static constexpr int kSourceSlot = 6;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaRingModCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
