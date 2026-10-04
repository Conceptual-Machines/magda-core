#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust waveshaper with six selectable curves.
 *
 * Drive pushes the input into the shape, Bias shifts its operating point,
 * Tone tilts the post-shape EQ and Mix blends the dry signal back in.
 */
class Saturator : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_saturator";

    Saturator();

    static constexpr int kDriveSlot = 0;
    static constexpr int kModeSlot = 1;
    static constexpr int kBiasSlot = 2;
    static constexpr int kToneSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Saturator";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_saturator_";
    }
};

}  // namespace magda::devices::faust
