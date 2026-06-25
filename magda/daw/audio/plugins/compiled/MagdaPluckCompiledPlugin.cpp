#include "plugins/compiled/MagdaPluckCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_pluck.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaPluckCompiledPlugin::xmlTypeName = "magda_pluck";

MagdaPluckCompiledPlugin::MagdaPluckCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaStrumInstrument(info) {
    initInstrument();
}

juce::String MagdaPluckCompiledPlugin::getName() const {
    return "Pluck";
}
juce::String MagdaPluckCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaPluckCompiledPlugin::getShortName(int) {
    return "Pluck";
}
juce::String MagdaPluckCompiledPlugin::getSelectableDescription() {
    return "Pluck";
}

::dsp* MagdaPluckCompiledPlugin::createVoiceDsp() const {
    return new MagdaPluckDsp();
}

std::vector<MagdaPluckCompiledPlugin::HostSlotInfo> MagdaPluckCompiledPlugin::voiceSlotInfos()
    const {
    using magda::ParameterScale;
    return {
        {.name = "Damping",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.7f},
        {.name = "Pluck Pos",
         .scale = ParameterScale::Linear,
         .minValue = 0.02f,
         .maxValue = 0.98f,
         .defaultValue = 0.35f},
        {.name = "Brightness",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
        {.name = "Drive",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.0f},
    };
}

const CompiledPluginSpec& getMagdaPluckSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPluckCompiledPlugin::xmlTypeName,
        .displayName = "Pluck",
        .browserCategory = "Synth",
        .description = "Curve-shaped plucked-string instrument: a held chord is strummed / "
                       "arpeggiated in time by a curve, each note sounded by a Karplus-Strong "
                       "pluck (Faust pm.lib). 32-voice, MIDI-driven.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaPluckCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
