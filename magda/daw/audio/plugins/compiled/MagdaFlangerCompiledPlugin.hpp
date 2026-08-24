#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust stereo flanger.
 *
 * Single short modulated delay (≈3 ms ± 2.5 ms) per channel with a
 * heavy feedback loop — the classic comb-sweep character. Rate is
 * either free (Hz) or tempo-synced via the shared musical-division
 * menu. Width spreads the L/R LFO phase offset.
 */
class MagdaFlangerCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaFlangerCompiledPlugin();

    static constexpr int kSyncSlot = 0;
    static constexpr int kRateSlot = 1;
    static constexpr int kDivisionSlot = 2;
    static constexpr int kDepthSlot = 3;
    static constexpr int kFeedbackSlot = 4;
    static constexpr int kMixSlot = 5;
    static constexpr int kWidthSlot = 6;
    static constexpr int kHostSlotCount = 7;
    static constexpr int kBpmSlot = 63;

    /// The Faust quarter-note multiplier behind Division choice @p index.
    float divisionFaustValueForIndex(int index) const {
        return menuValueForChoice(kDivisionSlot, index);
    }

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Flanger";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_flanger_";
    }
    double tailSeconds() const override {
        return 0.05;
    }
    bool resetsOnPlayStart() const override {
        return true;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFlangerCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
