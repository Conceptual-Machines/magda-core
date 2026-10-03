#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Three-engine compiled-Faust pitch shifter: Shifter, Detuner and Harmonizer, all on
 *        ef.transpose, whose transient smear and grain are the character.
 *
 * Shared zones are written into every engine each block, so switching keeps the settings.
 */
class Pitch : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_pitch";

    Pitch();

    static constexpr int kEngineSlot = 0;
    static constexpr int kPitchSlot = 1;
    static constexpr int kFineSlot = 2;
    static constexpr int kTextureSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;
    enum class PitchEngine { Shifter = 0, Detuner = 1, Harmonizer = 2 };
    static constexpr int kEngineCount = 3;

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Pitch";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_pitch_";
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
        // 200 ms max window, plus a safety margin.
        return 0.25;
    }
};

}  // namespace magda::devices::faust
