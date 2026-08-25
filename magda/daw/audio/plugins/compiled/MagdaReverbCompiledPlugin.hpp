#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Multi-engine compiled-Faust reverb.
 *
 * Engines:
 *  - Plate: Dattorro reverb (re.dattorro_rev). Dense allpass diffusion
 *    network, classic studio-plate sound. Backed by magda_reverb_plate.dsp.
 *  - Hall: Zita FDN reverb (re.zita_rev1_stereo). 8-tap feedback delay
 *    network with low/mid crossover decay; smooth large-space tails.
 *    Backed by magda_reverb_hall.dsp.
 *  - Room: Freeverb (re.stereo_freeverb). Schroeder/Moorer comb + allpass
 *    network, denser early reflections for small-space ambience. Backed
 *    by magda_reverb_room.dsp.
 *
 * All three engine DSPs are instantiated; only the active one runs
 * compute() per audio callback. Shared zones (Mix / Predelay / Decay /
 * Damping / Low Cut / High Cut / Width / Output) are written into every
 * engine every block so swapping engines preserves the user's settings.
 */
class MagdaReverbCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaReverbCompiledPlugin();

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

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Reverb";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
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
        // Conservative cap covering Hall's worst case (~15s at Decay=100):
        // the host keeps the device processing for this long after audio stops.
        return 10.0;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaReverbCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
