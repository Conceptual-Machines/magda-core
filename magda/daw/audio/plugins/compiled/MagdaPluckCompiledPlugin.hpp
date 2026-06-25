#pragma once

#include <vector>

#include "MagdaStrumInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust plucked-string instrument (the "Pluck" device).
 *
 * A pm.lib Karplus-Strong string (magda_pluck.dsp) strummed by the shared
 * MagdaStrumInstrument scheduler: a held chord is plucked in time by a curve to
 * become an expressive strum / roll / arpeggio. All scheduling, voice
 * management and the output stage live in the base; this subclass only supplies
 * the voice dsp and its four timbre macros (Damping / Pluck Pos / Brightness /
 * Drive).
 */
class MagdaPluckCompiledPlugin : public MagdaStrumInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaPluckCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_pluck_";
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaPluckCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
