#include "plugins/compiled/MagdaMalletCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_mallet.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaMalletCompiledPlugin::xmlTypeName = "magda_mallet";

MagdaMalletCompiledPlugin::MagdaMalletCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaStrumInstrument(info) {
    initInstrument();
}

juce::String MagdaMalletCompiledPlugin::getName() const {
    return "Percussion";
}
juce::String MagdaMalletCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaMalletCompiledPlugin::getShortName(int) {
    return "Perc";
}
juce::String MagdaMalletCompiledPlugin::getSelectableDescription() {
    return "Percussion";
}

::dsp* MagdaMalletCompiledPlugin::createVoiceDsp() const {
    return new MagdaMalletDsp();
}

std::vector<MagdaMalletCompiledPlugin::HostSlotInfo> MagdaMalletCompiledPlugin::voiceSlotInfos()
    const {
    using magda::ParameterScale;
    return {
        {.name = "Strike Pos",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "Strike Cutoff",
         .unit = "Hz",
         .scale = ParameterScale::Logarithmic,
         .minValue = 20.0f,
         .maxValue = 20000.0f,
         .defaultValue = 6500.0f},
        {.name = "Strike Sharpness",
         .scale = ParameterScale::Linear,
         .minValue = 0.01f,
         .maxValue = 5.0f,
         .defaultValue = 0.5f},
        {.name = "Model",
         .scale = ParameterScale::Discrete,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.0f,
         .choices = {"Marimba", "Djembe"}},
        {.name = "Decay",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
    };
}

const CompiledPluginSpec& getMagdaMalletSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaMalletCompiledPlugin::xmlTypeName,
        .displayName = "Percussion",
        .browserCategory = "Synth",
        .description = "Struck modal-percussion instrument: selectable Marimba / Djembe (Faust "
                       "pm.lib), strummed / rolled in time by a curve like Pluck. 16-voice, "
                       "MIDI-driven.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaMalletCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
