#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust struck-modal djembe instrument (the "Djembe" device).
 *
 * A hand-drum membrane physical model (pm.djembe) driven by a strike exciter
 * (Position / Sharpness). The djembe model has no strike-tone input, so it
 * exposes only those two knobs (plus the inherited Gain). Follows the played
 * note; modal decay is intrinsic, so there is no damping knob. Voice allocation
 * and the output stage live in MagdaCompiledPolyInstrument.
 */
class MagdaDjembeCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaDjembeCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_djembe_";
    }
    int numVoices() const override {
        return 8;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaDjembeCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
