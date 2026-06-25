#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust struck-percussion instrument (the "Percussion" device).
 *
 * A pm.lib modal voice (magda_mallet.dsp) - selectable Marimba / Djembe. Voice
 * allocation and the output stage live in MagdaCompiledPolyInstrument; this
 * subclass only supplies the voice dsp and its macros (Strike Pos / Strike
 * Cutoff / Strike Sharpness / Model / Decay). The modal decay is intrinsic to
 * the model, so there is no damping control. For struck / rolled chords, put the
 * Strum MIDI effect (MidiStrumPlugin) in front of it.
 */
class MagdaMalletCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaMalletCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_mallet_";
    }
    // Percussion rarely needs deep polyphony, and ba.selectn runs both modal
    // voices per voice, so run leaner than Pluck's 32.
    int numVoices() const override {
        return 16;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaMalletCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
