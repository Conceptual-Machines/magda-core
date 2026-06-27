#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust drum-machine snare voice (the "Snare" device).
 *
 * A tuned additive body (synths.lib sy.additiveDrum) blended with a band-passed
 * noise burst for the snares (magda_snare.dsp). Voice allocation and the output
 * stage live in MagdaCompiledPolyInstrument; this subclass supplies the voice
 * dsp and its five knobs (Tune / Tone / Snappy / Attack / Decay). Knob-tuned and
 * MIDI-gated, so it drops cleanly onto a DrumGrid pad.
 */
class MagdaSnareCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaSnareCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_snare_";
    }
    int numVoices() const override {
        return 8;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaSnareCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
