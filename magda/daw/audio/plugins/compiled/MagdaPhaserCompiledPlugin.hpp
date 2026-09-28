#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust stereo phaser.
 *
 * Hosts magda_phaser.dsp as a native Tracktion plugin. Every host control maps
 * 1:1 to a Faust slot pinned by [idx:N].
 */
class MagdaPhaserCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaPhaserCompiledPlugin();

    static constexpr int kRateSlot = 0;
    static constexpr int kDepthSlot = 1;
    static constexpr int kFeedbackSlot = 2;
    static constexpr int kStagesSlot = 3;
    static constexpr int kMinHzSlot = 4;
    static constexpr int kMaxHzSlot = 5;
    static constexpr int kMixSlot = 6;
    static constexpr int kHostSlotCount = 7;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Phaser";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_phaser_";
    }
    void writeExtraZones(int engineIndex) override;

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPhaserCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
