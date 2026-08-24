#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

namespace magda::daw::audio::compiled {

class MagdaUtilityCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaUtilityCompiledPlugin();

    static constexpr int kGainSlot = 0;
    static constexpr int kPanSlot = 1;
    static constexpr int kWidthSlot = 2;
    static constexpr int kLowMonoFreqSlot = 3;
    static constexpr int kMonoSlot = 4;
    static constexpr int kLowMonoSlot = 5;
    static constexpr int kFlipLSlot = 6;
    static constexpr int kFlipRSlot = 7;
    static constexpr int kHostSlotCount = 8;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Utility";
    }
    juce::String deviceShortName() const override {
        return "Util";
    }

  protected:
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_utility_";
    }
    void onReset() override;
    void processAudio(DeviceProcessContext& context) override;

  private:
    // One-pole state for the Low Mono crossover, audio thread only.
    float lowMonoLpL1_ = 0.0f;
    float lowMonoLpL2_ = 0.0f;
    float lowMonoLpR1_ = 0.0f;
    float lowMonoLpR2_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaUtilityCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
