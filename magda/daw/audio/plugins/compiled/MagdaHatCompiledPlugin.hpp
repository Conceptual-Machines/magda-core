#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust drum-machine hi-hat voice (the "Hat" device).
 *
 * A phase-modulated metallic tone through a resonant lowpass (synths.lib sy.hat,
 * magda_hat.dsp). One device covers both closed and open hats: a short Decay is
 * a closed hat, a long Decay is an open hat. Voice allocation and the output
 * stage live in MagdaCompiledPolyInstrument; this subclass supplies the voice
 * dsp and its four knobs (Pitch / Tone / Attack / Decay). Knob-tuned and
 * MIDI-gated, so it drops cleanly onto a DrumGrid pad.
 */
class MagdaHatCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaHatCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_hat_";
    }
    int numVoices() const override {
        return 8;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaHatCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
