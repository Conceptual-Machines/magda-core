#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust waveshaper with six selectable curves.
 *
 * Drive pushes the input into the shape, Bias shifts its operating point,
 * Tone tilts the post-shape EQ and Mix blends the dry signal back in.
 */
class MagdaSaturatorCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaSaturatorCompiledPlugin();

    static constexpr int kDriveSlot = 0;
    static constexpr int kModeSlot = 1;
    static constexpr int kBiasSlot = 2;
    static constexpr int kToneSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Saturator";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_saturator_";
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSaturatorCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
