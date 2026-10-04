#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust stereo digital delay: tempo sync, feedback tone tilt and ping-pong
 *        cross-feedback, every control pinned to its slot by [idx:N].
 *
 * The hidden BPM slot ([idx:63]) follows the project tempo, so Division tracks it.
 */
class Delay : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_delay";

    Delay();

    static constexpr int kTimeSlot = 0;
    static constexpr int kDivisionSlot = 1;
    static constexpr int kSyncSlot = 2;
    static constexpr int kFeedbackSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kToneSlot = 5;
    static constexpr int kCrossSlot = 6;
    static constexpr int kHostSlotCount = 7;
    static constexpr int kBpmSlot = 63;  // hidden, populated from the transport

    /// The Faust quarter-note multiplier behind Division choice @p index.
    float divisionFaustValueForIndex(int index) const {
        return menuValueForChoice(kDivisionSlot, index);
    }

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Delay";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_delay_";
    }
    bool producesAudioWithoutInput() const override {
        // The line still holds up to four seconds of echoes when the dry input stops.
        return true;
    }
    double tailSeconds() const override {
        // The dsp's MAX_DELAY_SAMPLES / SR worst case.
        return 4.0;
    }
};

}  // namespace magda::devices::faust
