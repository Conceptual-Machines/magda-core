#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust drum-machine tom voice (the "Tom" device).
 *
 * A tuned sine with a downward pitch sweep under a percussive amp envelope
 * (magda_tom.dsp). Voice allocation and the output stage live in
 * MagdaCompiledPolyInstrument; this subclass supplies the voice dsp and its four
 * knobs (Tune / Bend / Attack / Decay). Knob-tuned and MIDI-gated, so it drops
 * cleanly onto a DrumGrid pad.
 */
class MagdaTomCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    MagdaTomCompiledPlugin();

    juce::String devicePluginId() const override {
        return xmlTypeName;
    }
    juce::String deviceName() const override {
        return "Tom";
    }

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_tom_";
    }
    int numVoices() const override {
        return 8;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaTomCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
