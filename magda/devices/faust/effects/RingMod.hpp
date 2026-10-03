#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust stereo ring modulator.
 *
 * Multiplies the input by a sine / triangle / square carrier in the
 * 1 Hz – 5 kHz range. Low frequencies act like a tremolo; audio-rate
 * frequencies give the classic metallic-clang ring-mod timbre.
 */
class RingMod : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_ring_mod";

    RingMod();

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

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Ring Mod";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_ring_mod_";
    }
    sdk::SidechainPort sidechainPort() const override {
        return sdk::monoAudioSidechain;  // Source=Sidechain takes the carrier from it.
    }
    int outputChannelCount() const override {
        return 2;
    }
    int inputChannelCount() const override {
        return 2;
    }
    bool resetsOnPlayStart() const override {
        return true;
    }
};

}  // namespace magda::devices::faust
