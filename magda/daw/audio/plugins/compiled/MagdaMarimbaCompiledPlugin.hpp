#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust struck-modal marimba instrument (the "Marimba" device).
 *
 * A tuned tone-bar-and-tube physical model (pm.marimba) driven by a strike
 * exciter (Position / Tone / Sharpness). Follows the played note; modal decay is
 * intrinsic to the model, so there is no damping knob. Voice allocation and the
 * output stage live in MagdaCompiledPolyInstrument; this subclass only supplies
 * the voice dsp and its strike knobs (the inherited Gain follows).
 */
class MagdaMarimbaCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaMarimbaCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_marimba_";
    }
    int numVoices() const override {
        return 8;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaMarimbaCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
