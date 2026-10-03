#pragma once

#include <atomic>
#include <vector>

#include "devices/faust/CompiledEffect.hpp"

namespace magda::devices::faust {

/**
 * @brief Compiled-Faust multi-mode antialiased clipper.
 *
 * Static nonlinearity device — no envelope, attack, or release. The user
 * picks one of five ADAA curves from aa.lib (Hard / Soft / Tanh /
 * Hyperbolic / Sine) and drives the input into it.
 */
class Clipper : public CompiledEffect {
  public:
    static constexpr const char* xmlTypeName = "magda_clipper";

    Clipper();

    static constexpr int kDriveSlot = 0;
    static constexpr int kModeSlot = 1;
    static constexpr int kOutputSlot = 2;
    static constexpr int kHostSlotCount = 3;

    enum class ClipperMode { Hard = 0, Soft, Tanh, Hyperbolic, Sine };
    static constexpr int kModeCount = 5;

    // Audio-thread metering tap for the transfer-curve dot.
    float getInputPeakDb() const {
        return inputPeakDb_.load(std::memory_order_relaxed);
    }

    std::string devicePluginId() const override {
        return xmlTypeName;
    }
    std::string deviceName() const override {
        return "Clipper";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<SlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_clipper_";
    }
    void beforeCompute(sdk::ProcessContext& context, int engineIndex) override;

  private:
    std::atomic<float> inputPeakDb_{-120.0f};
};

}  // namespace magda::devices::faust
