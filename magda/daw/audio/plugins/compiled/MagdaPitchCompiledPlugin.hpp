#pragma once

#include <vector>

#include "plugins/compiled/MagdaCompiledEffect.hpp"

class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Multi-engine compiled-Faust pitch shifter.
 *
 * Engines:
 *  - Shifter: single-voice transpose, full +/-24 st range. The workhorse.
 *  - Detuner: two voices anti-symmetrically detuned, hard-panned L/R for
 *    chorus-without-modulation widening.
 *  - Harmonizer: single shifted voice summed with dry - default Pitch is
 *    a perfect fifth, default Mix 0.5 so the harmony reads immediately.
 *
 * All three are built on ef.transpose (two-delay-line crossfade shifter),
 * which is a granular method - transients smear, the crossfade audibly
 * pumps on long windows, and small windows go grainy. Those artefacts are
 * the character; if you want clean PSOLA you want a different device.
 *
 * Pattern B layout (per the Dimension / Reverb wrappers): every engine
 * DSP is instantiated, only the active one's compute() runs each block.
 * Shared zones (Pitch / Fine / Texture / Mix / Output) are written into
 * every engine every block so swapping engines preserves the user's
 * settings.
 */
class MagdaPitchCompiledPlugin : public MagdaCompiledEffect {
  public:
    static const char* xmlTypeName;

    MagdaPitchCompiledPlugin();

    static constexpr int kEngineSlot = 0;
    static constexpr int kPitchSlot = 1;
    static constexpr int kFineSlot = 2;
    static constexpr int kTextureSlot = 3;
    static constexpr int kMixSlot = 4;
    static constexpr int kOutputSlot = 5;
    static constexpr int kHostSlotCount = 6;
    enum class PitchEngine { Shifter = 0, Detuner = 1, Harmonizer = 2 };
    static constexpr int kEngineCount = 3;

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Pitch";
    }

  protected:
    ::dsp* createEngineDsp(int engineIndex) const override;
    std::vector<HostSlotInfo> slotInfos() const override;
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

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPitchCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
