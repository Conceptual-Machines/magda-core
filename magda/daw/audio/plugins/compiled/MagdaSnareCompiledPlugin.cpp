#include "plugins/compiled/MagdaSnareCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_snare.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaSnareCompiledPlugin::xmlTypeName = "magda_snare";

MagdaSnareCompiledPlugin::MagdaSnareCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaSnareCompiledPlugin::getName() const {
    return "Snare";
}
juce::String MagdaSnareCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaSnareCompiledPlugin::getShortName(int) {
    return "Snare";
}
juce::String MagdaSnareCompiledPlugin::getSelectableDescription() {
    return "Snare";
}

::dsp* MagdaSnareCompiledPlugin::createVoiceDsp() const {
    return new MagdaSnareDsp();
}

std::vector<MagdaSnareCompiledPlugin::HostSlotInfo> MagdaSnareCompiledPlugin::voiceSlotInfos()
    const {
    using magda::ParameterScale;
    return {
        {.name = "Tune",
         .scale = ParameterScale::Linear,
         .minValue = 100.0f,
         .maxValue = 400.0f,
         .defaultValue = 180.0f},
        {.name = "Tone",
         .scale = ParameterScale::Linear,
         .minValue = 800.0f,
         .maxValue = 12000.0f,
         .defaultValue = 3000.0f},
        {.name = "Snappy",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.6f},
        {.name = "Attack",
         .scale = ParameterScale::Linear,
         .minValue = 0.001f,
         .maxValue = 0.1f,
         .defaultValue = 0.002f},
        {.name = "Decay",
         .scale = ParameterScale::Linear,
         .minValue = 0.02f,
         .maxValue = 1.5f,
         .defaultValue = 0.2f},
    };
}

const CompiledPluginSpec& getMagdaSnareSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaSnareCompiledPlugin::xmlTypeName,
        .displayName = "Snare",
        .browserCategory = "Drums",
        .description = "Old-school drum-machine snare: a tuned additive body blended with a "
                       "band-passed noise burst (Faust synths.lib). Knob-tuned, MIDI-gated - drop "
                       "it on a DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaSnareCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
