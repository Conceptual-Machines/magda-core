#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust standard digital delay.
 *
 * Stereo delay line with tempo-sync, feedback tone tilt, and ping-pong
 * cross-feedback. Single-engine compiled plugin — every user control
 * maps 1:1 to a Faust slot pinned by [idx:N].
 *
 * The hidden BPM slot ([idx:63]) is populated each block from TE's
 * transport so musical-division mode tracks the live tempo.
 */
class MagdaDelayCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaDelayCompiledPlugin();

    static constexpr int kTimeSlot = 0;
    static constexpr int kDivisionSlot = 1;
    static constexpr int kSyncSlot = 2;
    static constexpr int kFeedbackSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kToneSlot = 5;
    static constexpr int kCrossSlot = 6;
    static constexpr int kHostSlotCount = 7;
    static constexpr int kBpmSlot = 63;  // hidden, populated from TE transport

    /// The Faust quarter-note multiplier behind Division choice @p index.
    float divisionFaustValueForIndex(int index) const {
        return menuValueForChoice(kDivisionSlot, index);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Delay";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_delay_";
    }
    double tailSeconds() const override {
        // Matches the dsp's MAX_DELAY_SAMPLES / SR worst case.
        return 4.0;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDelayCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
