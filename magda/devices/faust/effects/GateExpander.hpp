#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/// Compiled-Faust stereo gate / downward expander with a linked peak detector.
class GateExpander : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_gate_expander";

    GateExpander();

    static constexpr int kAttackSlot = 0;
    static constexpr int kReleaseSlot = 1;
    static constexpr int kMixSlot = 2;
    static constexpr int kOutputSlot = 3;
    static constexpr int kThresholdSlot = 4;
    static constexpr int kRatioSlot = 5;
    static constexpr int kRangeSlot = 6;
    static constexpr int kHostSlotCount = 7;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Gate";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_gate_expander_";
    }
};

}  // namespace magda::devices::faust
