#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>

#include "CompiledFaustInterface.hpp"
#include "core/ParameterInfo.hpp"

namespace magda::daw::audio::compiled {

class MagdaUtilityCompiledPlugin : public te::Plugin, public ICompiledFaustPlugin {
  public:
    static const char* xmlTypeName;

    explicit MagdaUtilityCompiledPlugin(const te::PluginCreationInfo& info);
    ~MagdaUtilityCompiledPlugin() override;

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

    static constexpr int kGainSlot = 0;
    static constexpr int kPanSlot = 1;
    static constexpr int kWidthSlot = 2;
    static constexpr int kLowMonoFreqSlot = 3;
    static constexpr int kMonoSlot = 4;
    static constexpr int kLowMonoSlot = 5;
    static constexpr int kFlipLSlot = 6;
    static constexpr int kFlipRSlot = 7;
    static constexpr int kHostSlotCount = 8;

    te::AutomatableParameter* getSlotParameter(int slotIndex) const;

    float displayValueToNativeValue(int slotIndex, float displayValue) const;
    float nativeValueToDisplayValue(int slotIndex, float nativeValue) const;

    using HostSlotInfo = CompiledHostSlotInfo;
    const HostSlotInfo& getSlotInfo(int slotIndex) const;

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

    std::array<HostSlotInfo, kHostSlotCount> hostSlotInfo_;
    std::array<te::AutomatableParameter::Ptr, kHostSlotCount> hostParams_;
    std::array<juce::CachedValue<float>, kHostSlotCount> hostCached_;

    int sampleRate_ = 44100;
    float lowMonoLpL1_ = 0.0f;
    float lowMonoLpL2_ = 0.0f;
    float lowMonoLpR1_ = 0.0f;
    float lowMonoLpR2_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaUtilityCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
