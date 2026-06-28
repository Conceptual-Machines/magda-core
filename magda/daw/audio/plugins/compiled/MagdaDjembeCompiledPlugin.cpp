#include "plugins/compiled/MagdaDjembeCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_djembe.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaDjembeCompiledPlugin::xmlTypeName = "magda_djembe";

MagdaDjembeCompiledPlugin::MagdaDjembeCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaDjembeCompiledPlugin::getName() const {
    return "Djembe";
}
juce::String MagdaDjembeCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaDjembeCompiledPlugin::getShortName(int) {
    return "Djembe";
}
juce::String MagdaDjembeCompiledPlugin::getSelectableDescription() {
    return "Djembe";
}

::dsp* MagdaDjembeCompiledPlugin::createVoiceDsp() const {
    return new MagdaDjembeDsp();
}

std::vector<MagdaDjembeCompiledPlugin::HostSlotInfo> MagdaDjembeCompiledPlugin::voiceSlotInfos()
    const {
    using magda::ParameterScale;
    return {
        {.name = "strike_position",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
        {.name = "strike_sharpness",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 50.0f,
         .maxValue = 3000.0f,
         .defaultValue = 600.0f},
        {.name = "spacing",
         .unit = "Hz",
         .scale = ParameterScale::Linear,
         .minValue = 20.0f,
         .maxValue = 600.0f,
         .defaultValue = 200.0f},
        {.name = "inharmonicity",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.0f},
    };
}

const CompiledPluginSpec& getMagdaDjembeSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaDjembeCompiledPlugin::xmlTypeName,
        .displayName = "Djembe",
        .browserCategory = "Drums",
        .description = "Struck modal djembe: a hand-drum membrane physical model driven by a "
                       "strike exciter (Position / Sharpness). Follows the played note; modal "
                       "decay is intrinsic to the model, so there is no damping knob.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaDjembeCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
