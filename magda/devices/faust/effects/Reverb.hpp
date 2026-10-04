#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Three-engine compiled-Faust reverb: Plate (Dattorro), Hall (Zita FDN) and Room
 *        (Freeverb).
 *
 * Shared zones are written into every engine each block, so switching keeps the settings.
 */
class Reverb : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_reverb";

    Reverb();

    static constexpr int kEngineSlot = 0;
    static constexpr int kMixSlot = 1;
    static constexpr int kPredelaySlot = 2;
    static constexpr int kDecaySlot = 3;
    static constexpr int kDampingSlot = 4;
    static constexpr int kLowCutSlot = 5;
    static constexpr int kHighCutSlot = 6;
    static constexpr int kWidthSlot = 7;
    static constexpr int kOutputSlot = 8;
    static constexpr int kHostSlotCount = 9;
    enum class ReverbEngine { Plate = 0, Hall = 1, Room = 2 };
    static constexpr int kEngineCount = 3;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Reverb";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_reverb_";
    }
    int engineCount() const override {
        return kEngineCount;
    }
    int engineSlot() const override {
        return kEngineSlot;
    }
    int outputChannelCount() const override {
        return 2;
    }
    bool producesAudioWithoutInput() const override {
        return true;
    }
    double tailSeconds() const override {
        // A conservative cap on Hall's worst case (~15 s at Decay 100).
        return 10.0;
    }
};

}  // namespace magda::devices::faust
