#include "plugins/compiled/MagdaClapCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_clap.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaClapCompiledPlugin::xmlTypeName = "magda_clap";

MagdaClapCompiledPlugin::MagdaClapCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaClapCompiledPlugin::getName() const {
    return "Clap";
}
juce::String MagdaClapCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaClapCompiledPlugin::getShortName(int) {
    return "Clap";
}
juce::String MagdaClapCompiledPlugin::getSelectableDescription() {
    return "Clap";
}

::dsp* MagdaClapCompiledPlugin::createVoiceDsp() const {
    return new MagdaClapDsp();
}

std::vector<MagdaClapCompiledPlugin::HostSlotInfo> MagdaClapCompiledPlugin::voiceSlotInfos() const {
    using magda::ParameterScale;
    return {
        {.name = "Tone",
         .scale = ParameterScale::Linear,
         .minValue = 400.0f,
         .maxValue = 3500.0f,
         .defaultValue = 1500.0f},
        {.name = "Attack",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 0.2f,
         .defaultValue = 0.0f},
        {.name = "Decay",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 2.0f,
         .defaultValue = 0.0f},
    };
}

const CompiledPluginSpec& getMagdaClapSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaClapCompiledPlugin::xmlTypeName,
        .displayName = "Clap",
        .browserCategory = "Drums",
        .description = "Old-school drum-machine clap: four offset noise bursts through a resonant "
                       "lowpass (Faust synths.lib). Knob-tuned, MIDI-gated - drop it on a DrumGrid "
                       "pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaClapCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
