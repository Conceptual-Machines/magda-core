#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust drum-machine kick voice (the "Kick" device).
 *
 * An 808/909-style pitched sine sweep into a saturator (synths.lib sy.kick,
 * magda_kick.dsp). Voice allocation and the output stage live in
 * MagdaCompiledPolyInstrument; this subclass only supplies the voice dsp and its
 * five knobs (Pitch / Click / Attack / Decay / Drive). Knob-tuned: the played
 * MIDI note only gates the voice, so it drops cleanly onto a DrumGrid pad.
 */
class MagdaKickCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaKickCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_kick_";
    }
    int numVoices() const override {
        return 8;  // drum one-shots: a small pool covers overlapping tails
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaKickCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
