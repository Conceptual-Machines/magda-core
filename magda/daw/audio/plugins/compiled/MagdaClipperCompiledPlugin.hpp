#pragma once

#include <atomic>
#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust multi-mode antialiased clipper.
 *
 * Static nonlinearity device — no envelope, attack, or release. The user
 * picks one of five ADAA curves from aa.lib (Hard / Soft / Tanh /
 * Hyperbolic / Sine) and drives the input into it.
 */
class MagdaClipperCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaClipperCompiledPlugin();

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

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Clipper";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_clipper_";
    }
    void beforeCompute(DeviceProcessContext& context, int engineIndex) override;

  private:
    std::atomic<float> inputPeakDb_{-120.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaClipperCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
