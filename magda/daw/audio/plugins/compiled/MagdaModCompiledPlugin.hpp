#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust modulation device (tremolo / vibrato / auto-pan).
 *
 * One LFO drives three switchable mode bodies; rate is either free (Hz) or
 * synced to project tempo via a musical-division menu. The hidden BPM slot
 * ([idx:63]) is populated each block from TE's transport so sync mode tracks
 * the live tempo — same plumbing the delay uses.
 */
class MagdaModCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaModCompiledPlugin();

    static constexpr int kModeSlot = 0;
    static constexpr int kSyncSlot = 1;
    static constexpr int kRateSlot = 2;
    static constexpr int kDivisionSlot = 3;
    static constexpr int kDepthSlot = 4;
    static constexpr int kShapeSlot = 5;
    static constexpr int kHostSlotCount = 6;
    static constexpr int kBpmSlot = 63;  // hidden, host-driven

    /// The Faust quarter-note multiplier behind Division choice @p index.
    float divisionFaustValueForIndex(int index) const {
        return menuValueForChoice(kDivisionSlot, index);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Mod";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_mod_";
    }
    bool resetsOnPlayStart() const override {
        return true;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaModCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
