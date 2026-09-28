#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

class MagdaGateExpanderCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaGateExpanderCompiledPlugin();

    static constexpr int kAttackSlot = 0;
    static constexpr int kReleaseSlot = 1;
    static constexpr int kMixSlot = 2;
    static constexpr int kOutputSlot = 3;
    static constexpr int kThresholdSlot = 4;
    static constexpr int kRatioSlot = 5;
    static constexpr int kRangeSlot = 6;
    static constexpr int kHostSlotCount = 7;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Gate";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_gate_expander_";
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaGateExpanderCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
