#include "plugins/compiled/MagdaMarimbaCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_marimba.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaMarimbaCompiledPlugin::xmlTypeName = "magda_marimba";

MagdaMarimbaCompiledPlugin::MagdaMarimbaCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaMarimbaCompiledPlugin::getName() const {
    return "Marimba";
}
juce::String MagdaMarimbaCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaMarimbaCompiledPlugin::getShortName(int) {
    return "Marimba";
}
juce::String MagdaMarimbaCompiledPlugin::getSelectableDescription() {
    return "Marimba";
}

::dsp* MagdaMarimbaCompiledPlugin::createVoiceDsp() const {
    return new MagdaMarimbaDsp();
}

std::vector<MagdaMarimbaCompiledPlugin::HostSlotInfo> MagdaMarimbaCompiledPlugin::voiceSlotInfos()
    const {
    using magda::ParameterScale;
    return {
        {.name = "strike_position",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.3f},
        {.name = "strike_tone",
         .unit = "Hz",
         .scale = ParameterScale::Logarithmic,
         .minValue = 500.0f,
         .maxValue = 12000.0f,
         .defaultValue = 7000.0f},
        {.name = "strike_sharpness",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.25f},
        {.name = "decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 50.0f,
         .maxValue = 2000.0f,
         .defaultValue = 100.0f},
    };
}

const CompiledPluginSpec& getMagdaMarimbaSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaMarimbaCompiledPlugin::xmlTypeName,
        .displayName = "Marimba",
        .browserCategory = "Synth",
        .description = "Struck modal marimba: a tuned tone-bar-and-tube physical model driven by a "
                       "strike exciter (Position / Tone / Sharpness). Follows the played note; "
                       "modal decay is intrinsic to the model, so there is no damping knob.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaMarimbaCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
