#include "plugins/compiled/MagdaBellCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_bell.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaBellCompiledPlugin::xmlTypeName = "magda_bell";

MagdaBellCompiledPlugin::MagdaBellCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaBellCompiledPlugin::getName() const {
    return "Bell";
}
juce::String MagdaBellCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaBellCompiledPlugin::getShortName(int) {
    return "Bell";
}
juce::String MagdaBellCompiledPlugin::getSelectableDescription() {
    return "Bell";
}

::dsp* MagdaBellCompiledPlugin::createVoiceDsp() const {
    return new MagdaBellDsp();
}

std::vector<MagdaBellCompiledPlugin::HostSlotInfo> MagdaBellCompiledPlugin::voiceSlotInfos() const {
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
         .minValue = 300.0f,
         .maxValue = 40000.0f,
         .defaultValue = 8000.0f},
    };
}

const CompiledPluginSpec& getMagdaBellSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaBellCompiledPlugin::xmlTypeName,
        .displayName = "Bell",
        .browserCategory = "Drums",
        .description = "Struck modal bell: a church-bell physical model driven by a strike exciter "
                       "(Position / Tone / Sharpness). Fixed-pitch, so the played note only "
                       "triggers it. Modal decay is intrinsic to the model, so there is no "
                       "damping knob.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaBellCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
