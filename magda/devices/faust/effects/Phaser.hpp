#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust stereo phaser.
 *
 * Hosts magda_phaser.dsp as a native plugin. Every host control maps
 * 1:1 to a Faust slot pinned by [idx:N].
 */
class Phaser : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_phaser";

    Phaser();

    static constexpr int kRateSlot = 0;
    static constexpr int kDepthSlot = 1;
    static constexpr int kFeedbackSlot = 2;
    static constexpr int kStagesSlot = 3;
    static constexpr int kMinHzSlot = 4;
    static constexpr int kMaxHzSlot = 5;
    static constexpr int kMixSlot = 6;
    static constexpr int kHostSlotCount = 7;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Phaser";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_phaser_";
    }
    void writeExtraZones(int engineIndex) override;
};

}  // namespace magda::devices::faust
