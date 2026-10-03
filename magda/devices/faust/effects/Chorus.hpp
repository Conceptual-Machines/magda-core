#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust stereo chorus.
 *
 * 1–3 modulated delay voices per channel sharing one LFO. Rate is either
 * free (Hz) or tempo-synced via a musical-division menu — same plumbing
 * the delay / mod devices use. Width spreads voice phases and L/R offset.
 */
class Chorus : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_chorus";

    Chorus();

    static constexpr int kVoicesSlot = 0;
    static constexpr int kSyncSlot = 1;
    static constexpr int kRateSlot = 2;
    static constexpr int kDivisionSlot = 3;
    static constexpr int kDepthSlot = 4;
    static constexpr int kFeedbackSlot = 5;
    static constexpr int kMixSlot = 6;
    static constexpr int kWidthSlot = 7;
    static constexpr int kHostSlotCount = 8;
    static constexpr int kBpmSlot = 63;

    /// The Faust quarter-note multiplier behind Division choice @p index.
    float divisionFaustValueForIndex(int index) const {
        return menuValueForChoice(kDivisionSlot, index);
    }

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Chorus";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_chorus_";
    }
    double tailSeconds() const override {
        return 0.1;
    }
    bool resetsOnPlayStart() const override {
        return true;
    }
};

}  // namespace magda::devices::faust
