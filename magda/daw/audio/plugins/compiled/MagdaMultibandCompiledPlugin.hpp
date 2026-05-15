#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <memory>
#include <vector>

#include "../FaustParamPool.hpp"
#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust OTT-style 3-band compressor.
 *
 * Linkwitz-Riley splits the input into low / mid / high bands; each band
 * runs feed-forward parallel up + down compression; the three bands are
 * makeup-gained and summed back to stereo.
 *
 * Single-engine compiled plugin — all host controls map 1:1 to
 * Faust slots pinned by [idx:N].
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

    // Slot ordering matches the [idx:N] pins inside magda_multiband.dsp.
    // Slots 0-8: knob-only controls shown in the param grid.
    // Slots 9-26: per-band controls edited on the curve view (hidden from grid).
    // Slots 27-28: crossover frequencies, editor-only.
    static constexpr int kDepthSlot = 0;
    static constexpr int kTimeSlot = 1;
    static constexpr int kDepth2Slot = 2;
    static constexpr int kTime2Slot = 3;
    static constexpr int kLowGainSlot = 4;
    static constexpr int kMidGainSlot = 5;
    static constexpr int kHighGainSlot = 6;
    static constexpr int kMixSlot = 7;
    static constexpr int kOutputSlot = 8;
    static constexpr int kLowThreshAboveSlot = 9;
    static constexpr int kLowThreshBelowSlot = 10;
    static constexpr int kLowRatioAboveSlot = 11;
    static constexpr int kLowRatioBelowSlot = 12;
    static constexpr int kLowThreshExpandSlot = 13;
    static constexpr int kLowExpandRatioSlot = 14;
    static constexpr int kMidThreshAboveSlot = 15;
    static constexpr int kMidThreshBelowSlot = 16;
    static constexpr int kMidRatioAboveSlot = 17;
    static constexpr int kMidRatioBelowSlot = 18;
    static constexpr int kMidThreshExpandSlot = 19;
    static constexpr int kMidExpandRatioSlot = 20;
    static constexpr int kHighThreshAboveSlot = 21;
    static constexpr int kHighThreshBelowSlot = 22;
    static constexpr int kHighRatioAboveSlot = 23;
    static constexpr int kHighRatioBelowSlot = 24;
    static constexpr int kHighThreshExpandSlot = 25;
    static constexpr int kHighExpandRatioSlot = 26;
    static constexpr int kLowXoSlot = 27;   // editor-only, not in param grid
    static constexpr int kHighXoSlot = 28;  // editor-only, not in param grid
    static constexpr int kHostSlotCount = 29;

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
    void rebuildEngineState(int sampleRate);

    std::unique_ptr<::dsp> dsp_;
    int numInputs_ = 0;
    int numOutputs_ = 0;

    std::array<FAUSTFLOAT*, kHostSlotCount> zones_{};

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;
    juce::CachedValue<bool> curveCollapsed_;

    juce::AudioBuffer<float> scratchIn_;
    juce::AudioBuffer<float> scratchOut_;
    std::vector<float*> inPtrs_;
    std::vector<float*> outPtrs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaMultibandCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
