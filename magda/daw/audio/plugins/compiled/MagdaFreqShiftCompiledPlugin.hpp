#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust stereo single-sideband frequency shifter.
 *
 * Bode-style: a Hilbert transformer splits the input into 0° and 90°
 * paths, those are complex-multiplied with a phasor at the shift
 * frequency, and only the real part of the result is taken. Shifts the
 * entire spectrum by a constant Hz offset (unlike ring mod, which
 * produces sum + difference sidebands).
 */
class MagdaFreqShiftCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaFreqShiftCompiledPlugin();

    static constexpr int kShiftSlot = 0;
    static constexpr int kFeedbackSlot = 1;
    static constexpr int kMixSlot = 2;
    static constexpr int kSpreadSlot = 3;
    static constexpr int kHostSlotCount = 4;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Freq Shift";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_freq_shift_";
    }
    bool resetsOnPlayStart() const override {
        return true;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFreqShiftCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
