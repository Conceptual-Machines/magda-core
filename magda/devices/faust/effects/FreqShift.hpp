#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust stereo single-sideband frequency shifter.
 *
 * Bode-style: a Hilbert transformer splits the input into 0° and 90°
 * paths, those are complex-multiplied with a phasor at the shift
 * frequency, and only the real part of the result is taken. Shifts the
 * entire spectrum by a constant Hz offset (unlike ring mod, which
 * produces sum + difference sidebands).
 */
class FreqShift : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_freq_shift";

    FreqShift();

    static constexpr int kShiftSlot = 0;
    static constexpr int kFeedbackSlot = 1;
    static constexpr int kMixSlot = 2;
    static constexpr int kSpreadSlot = 3;
    static constexpr int kHostSlotCount = 4;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Freq Shift";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_freq_shift_";
    }
    bool resetsOnPlayStart() const override {
        return true;
    }
};

}  // namespace magda::devices::faust
