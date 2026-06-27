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
        {.name = "flam_tone",
         .scale = ParameterScale::Linear,
         .minValue = 400.0f,
         .maxValue = 4000.0f,
         .defaultValue = 1000.0f},
        {.name = "flam_spread",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 2.0f,
         .maxValue = 30.0f,
         .defaultValue = 9.0f},
        {.name = "flam_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 20.0f,
         .maxValue = 1500.0f,
         .defaultValue = 200.0f},
        {.name = "flam_tail",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
        {.name = "shape_hp_freq",
         .scale = ParameterScale::Linear,
         .minValue = 500.0f,
         .maxValue = 8000.0f,
         .defaultValue = 2000.0f},
        {.name = "shape_hp_reso",
         .scale = ParameterScale::Linear,
         .minValue = 0.5f,
         .maxValue = 10.0f,
         .defaultValue = 1.0f},
        {.name = "shape_drive",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 20.0f,
         .defaultValue = 1.0f},
        {.name = "flam_curve",
         .scale = ParameterScale::Linear,
         .minValue = -50.0f,
         .maxValue = 50.0f,
         .defaultValue = 0.0f},
    };
}

const CompiledPluginSpec& getMagdaClapSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaClapCompiledPlugin::xmlTypeName,
        .displayName = "Clap",
        .browserCategory = "Drums",
        .description = "Synthetic clap: one band-passed noise source shaped by a fast "
                       "three-triangle envelope (the hand-clap flam), over a diffuse tail. "
                       "Knob-tuned, MIDI-gated - drop it on a DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaClapCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
