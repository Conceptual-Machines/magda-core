#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust stereo granular delay.
 *
 * Same host contract as the compiled delay: controls are pinned by [idx:N],
 * the hidden BPM slot is host-written every block, and the Division menu
 * stores a display index while audio receives the Faust-side division value.
 */
class MagdaGrainDelayCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaGrainDelayCompiledPlugin();

    static constexpr int kTimeSlot = 0;
    static constexpr int kDivisionSlot = 1;
    static constexpr int kSyncSlot = 2;
    static constexpr int kSizeSlot = 3;
    static constexpr int kPitchSlot = 4;
    static constexpr int kSpraySlot = 5;
    static constexpr int kFeedbackSlot = 6;
    static constexpr int kMixSlot = 7;
    static constexpr int kHostSlotCount = 8;
    static constexpr int kBpmSlot = 63;

    /// The Faust quarter-note multiplier behind Division choice @p index.
    float divisionFaustValueForIndex(int index) const {
        return menuValueForChoice(kDivisionSlot, index);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Grain Delay";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_grain_delay_";
    }
    double tailSeconds() const override {
        return 4.0;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaGrainDelayCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
