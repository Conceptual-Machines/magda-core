#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust Erosion-style texturizer.
 *
 * Ring-modulates the input with a band-passed noise (or sine) carrier.
 * Mode picks the carrier source: Noise (mono BPF'd noise), Wide Noise
 * (decorrelated stereo BPF'd noise), Sine (a tonal carrier at the
 * Frequency knob).
 *
 * Single-engine compiled plugin — every host control maps 1:1 to a
 * Faust slot pinned by [idx:N], same harvest pattern as the saturator.
 */
class MagdaGritCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaGritCompiledPlugin();

    static constexpr int kFrequencySlot = 0;
    static constexpr int kWidthSlot = 1;
    static constexpr int kAmountSlot = 2;
    static constexpr int kModeSlot = 3;
    static constexpr int kHostSlotCount = 4;
    enum class Mode { Noise, WideNoise, Sine };
    static constexpr int kModeCount = 3;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Grit";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_grit_";
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaGritCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
