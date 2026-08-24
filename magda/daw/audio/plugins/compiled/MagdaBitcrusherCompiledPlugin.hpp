#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust bitcrusher.
 *
 * Sample-rate and bit-depth reduction with pre-crush drive and a
 * post-crush tone (1-pole low-pass). Single-engine compiled plugin,
 * same single-engine harvest pattern as Grit / Saturator / Delay.
 */
class MagdaBitcrusherCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaBitcrusherCompiledPlugin();

    static constexpr int kRateSlot = 0;
    static constexpr int kBitsSlot = 1;
    static constexpr int kDriveSlot = 2;
    static constexpr int kToneSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Bitcrusher";
    }
    juce::String deviceShortName() const override {
        return "Crush";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_bitcrusher_";
    }
    int outputChannelCount() const override {
        return 2;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaBitcrusherCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
