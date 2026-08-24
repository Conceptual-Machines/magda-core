#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust stereo ring modulator.
 *
 * Multiplies the input by a sine / triangle / square carrier in the
 * 1 Hz – 5 kHz range. Low frequencies act like a tremolo; audio-rate
 * frequencies give the classic metallic-clang ring-mod timbre.
 */
class MagdaRingModCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaRingModCompiledPlugin();

    static constexpr int kSyncSlot = 0;
    static constexpr int kFrequencySlot = 1;
    static constexpr int kDivisionSlot = 2;
    static constexpr int kShapeSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kWidthSlot = 5;
    static constexpr int kSourceSlot = 6;
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
        return "Ring Mod";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_ring_mod_";
    }
    bool wantsSidechain() const override {
        return true;  // Source=Sidechain takes the carrier from input 3.
    }
    bool resetsOnPlayStart() const override {
        return true;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaRingModCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
