#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust four-operator FM synth with a full 4x4 modulation matrix.
 *
 * Four operators (selectable Sine / Triangle / Saw / Square / Noise) cross-modulate
 * through a 4x4 matrix - every operator can phase-modulate any operator, the
 * diagonal being self-feedback - so it spans every DX-style algorithm and the
 * unstable territory between them. Per-op ratio + output level and a shared amp
 * ADSR. Voice allocation and the output stage come from
 * MagdaCompiledPolyInstrument; this subclass only supplies the dsp and the slot
 * table.
 *
 * Slot layout (matches magda_fm.dsp [idx:N]):
 *   0..15  matrix M(src,dst), row-major idx = src*4 + dst
 *   16..19 Op ratio
 *   20..23 Op level
 *   24..27 Amp ADSR
 *   28..31 Op waveform
 */
class MagdaFMCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaFMCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_fm_";
    }
    int numVoices() const override {
        return 16;
    }
    bool hasVoiceModes() const override {
        return true;
    }
    int glideVoiceSlot() const override {
        return 32;  // magda_fm.dsp Glide [idx:32]
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaFMCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
