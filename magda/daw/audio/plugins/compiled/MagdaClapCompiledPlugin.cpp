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
         .minValue = 500.0f,
         .maxValue = 3000.0f,
         .defaultValue = 1200.0f},
        {.name = "Spread",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 3.0f,
         .maxValue = 30.0f,
         .defaultValue = 9.0f},
        {.name = "Decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 20.0f,
         .maxValue = 1000.0f,
         .defaultValue = 200.0f},
        {.name = "Tail",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
    };
}

const CompiledPluginSpec& getMagdaClapSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaClapCompiledPlugin::xmlTypeName,
        .displayName = "Clap",
        .browserCategory = "Drums",
        .description = "Synthetic clap: a band-passed noise burst plus two delayed copies (Spread "
                       "spacing) for the hand-clap flam, over a longer diffuse tail. Knob-tuned, "
                       "MIDI-gated - drop it on a DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaClapCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
