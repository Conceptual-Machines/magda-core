#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust bitcrusher.
 *
 * Sample-rate and bit-depth reduction with pre-crush drive and a
 * post-crush tone (1-pole low-pass). Single-engine compiled plugin,
 * same single-engine harvest pattern as Grit / Saturator / Delay.
 */
class Bitcrusher : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_bitcrusher";

    Bitcrusher();

    static constexpr int kRateSlot = 0;
    static constexpr int kBitsSlot = 1;
    static constexpr int kDriveSlot = 2;
    static constexpr int kToneSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Bitcrusher";
    }
    std::string deviceShortName() const override {
        return "Crush";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_bitcrusher_";
    }
    int outputChannelCount() const override {
        return 2;
    }
};

}  // namespace magda::devices::faust
