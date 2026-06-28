#pragma once

#include <vector>

#include "MagdaCompiledPolyInstrument.hpp"

// The single-voice dsp is forward-declared via its Faust base; the .cpp owns it.
class dsp;

namespace magda::daw::audio::compiled {

/**
 * @brief Compiled-Faust struck-modal bell instrument (the "Bell" device).
 *
 * A church-bell physical model (pm.churchBell) driven by a strike exciter
 * (Position / Tone / Sharpness). The bell is a fixed-pitch model with no
 * frequency input, so the played note only triggers it (like the drum voices).
 * Modal decay is intrinsic, so there is no damping knob. Voice allocation and the
 * output stage live in MagdaCompiledPolyInstrument.
 */
class MagdaBellCompiledPlugin : public MagdaCompiledPolyInstrument {
  public:
    static const char* xmlTypeName;

    explicit MagdaBellCompiledPlugin(const te::PluginCreationInfo& info);

    juce::String getName() const override;
    juce::String getPluginType() override;
    juce::String getShortName(int) override;
    juce::String getSelectableDescription() override;

  protected:
    ::dsp* createVoiceDsp() const override;
    std::vector<HostSlotInfo> voiceSlotInfos() const override;
    const char* slotIdPrefix() const override {
        return "magda_bell_";
    }
    int numVoices() const override {
        return 8;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagdaBellCompiledPlugin)
};

}  // namespace magda::daw::audio::compiled
