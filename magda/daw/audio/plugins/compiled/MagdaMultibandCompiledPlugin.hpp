#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>

#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

namespace magda::daw::audio::compiled {

/**
 * @brief Native 3-band dynamics processor with a single flexible transfer curve per band.
 */
class MagdaMultibandCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaMultibandCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaMultibandCompiledPlugin() override;

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

    // Slots 0-8 are the compact grid controls.
    // Slots 9-20 are per-band curve controls edited from the custom panel.
    // Slots 21-22 are crossover frequencies.
    static constexpr int kAmountSlot = 0;
    static constexpr int kAttackSlot = 1;
    static constexpr int kReleaseSlot = 2;
    static constexpr int kInputSlot = 3;
    static constexpr int kLowGainSlot = 4;
    static constexpr int kMidGainSlot = 5;
    static constexpr int kHighGainSlot = 6;
    static constexpr int kMixSlot = 7;
    static constexpr int kOutputSlot = 8;
    static constexpr int kLowThresholdSlot = 9;
    static constexpr int kLowRatioSlot = 10;
    static constexpr int kLowRangeSlot = 11;
    static constexpr int kLowLimitSlot = 12;
    static constexpr int kMidThresholdSlot = 13;
    static constexpr int kMidRatioSlot = 14;
    static constexpr int kMidRangeSlot = 15;
    static constexpr int kMidLimitSlot = 16;
    static constexpr int kHighThresholdSlot = 17;
    static constexpr int kHighRatioSlot = 18;
    static constexpr int kHighRangeSlot = 19;
    static constexpr int kHighLimitSlot = 20;
    static constexpr int kLowXoSlot = 21;
    static constexpr int kHighXoSlot = 22;
    static constexpr int kHostSlotCount = 23;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

    bool isCurveCollapsed() const {
        return curveCollapsed_.get();
    }
    void setCurveCollapsed(bool collapsed) {
        curveCollapsed_ = collapsed;
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
    float slotDisplayValue(int slotIndex) const;
    void updateCrossoverCoefficients(float lowXoHz, float highXoHz);

    struct Biquad {
        void setLowPass(double sampleRate, double frequency);
        void setHighPass(double sampleRate, double frequency);
        void reset();
        float process(float x);

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };

    struct CrossoverState {
        void setCoefficients(double sampleRate, double lowHz, double highHz);
        void reset();
        void split(float input, float& low, float& mid, float& high);

        Biquad lowLp1, lowLp2;
        Biquad splitHp1, splitHp2;
        Biquad midLp1, midLp2;
        Biquad highHp1, highHp2;
    };

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;
    juce::CachedValue<bool> curveCollapsed_;

    double sampleRate_ = 44100.0;
    std::array<CrossoverState, 2> crossovers_;
    std::array<std::array<float, 3>, 2> envelopes_{};
    std::array<std::array<float, 3>, 2> gainDb_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaMultibandCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
