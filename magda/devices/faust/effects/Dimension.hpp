#pragma once

#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Three-engine compiled-Faust stereo widener: Dimension (D-style anti-phase modulated
 *        delays), Haas (a short fixed delay on one side) and M/S (side gain).
 *
 * Shared zones are written into every engine each block, so switching keeps the settings.
 */
class Dimension : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_dimension";

    Dimension();

    static constexpr int kEngineSlot = 0;
    static constexpr int kAmountSlot = 1;
    static constexpr int kRateSlot = 2;
    static constexpr int kWidthSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;
    enum class DimensionEngine { Dimension = 0, Haas = 1, MidSide = 2 };
    static constexpr int kEngineCount = 3;

    bool isSlotHiddenForActiveEngine(int slotIndex) const override {
        // Rate drives only the Dimension engine's modulator; Faust strips it from the others.
        return slotIndex == kRateSlot &&
               activeEngine() != static_cast<int>(DimensionEngine::Dimension);
    }

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Dimension";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_dimension_";
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
        // Haas at 30 ms, times a safety margin.
        return 0.1;
    }
};

}  // namespace magda::devices::faust
