#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/// Stereo utility: gain, pan, M/S width, mono, low mono and polarity flips. No Faust engine.
class Utility : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_utility";

    Utility();

    static constexpr int kGainSlot = 0;
    static constexpr int kPanSlot = 1;
    static constexpr int kWidthSlot = 2;
    static constexpr int kLowMonoFreqSlot = 3;
    static constexpr int kMonoSlot = 4;
    static constexpr int kLowMonoSlot = 5;
    static constexpr int kFlipLSlot = 6;
    static constexpr int kFlipRSlot = 7;
    static constexpr int kHostSlotCount = 8;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Utility";
    }
    std::string deviceShortName() const override {
        return "Util";
    }

  protected:
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_utility_";
    }
    void onReset() override;
    void processAudio(sdk::ProcessContext& context) override;

  private:
    // One-pole state for the Low Mono crossover, audio thread only.
    float lowMonoLpL1_ = 0.0f;
    float lowMonoLpL2_ = 0.0f;
    float lowMonoLpR1_ = 0.0f;
    float lowMonoLpR2_ = 0.0f;
};

}  // namespace magda::devices::faust
