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
        // Transient
        {.name = "Transient",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
        {.name = "Trans Tone",
         .scale = ParameterScale::Linear,
         .minValue = 1000.0f,
         .maxValue = 12000.0f,
         .defaultValue = 4000.0f},
        // Body
        {.name = "Tune",
         .scale = ParameterScale::Linear,
         .minValue = 100.0f,
         .maxValue = 400.0f,
         .defaultValue = 180.0f},
        {.name = "Snap",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.25f},
        {.name = "Snap Time",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 2.0f,
         .maxValue = 80.0f,
         .defaultValue = 12.0f},
        {.name = "Attack",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 100.0f,
         .defaultValue = 0.0f},
        {.name = "Body Decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 1500.0f,
         .defaultValue = 180.0f},
        // Rattle / tail
        {.name = "Snappy",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.6f},
        {.name = "Tone",
         .scale = ParameterScale::Linear,
         .minValue = 800.0f,
         .maxValue = 12000.0f,
         .defaultValue = 3000.0f},
        {.name = "HP Freq",
         .scale = ParameterScale::Linear,
         .minValue = 20.0f,
         .maxValue = 6000.0f,
         .defaultValue = 300.0f},
        {.name = "HP Reso",
         .scale = ParameterScale::Linear,
         .minValue = 0.5f,
         .maxValue = 10.0f,
         .defaultValue = 0.7f},
        {.name = "Rattle Decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 1500.0f,
         .defaultValue = 200.0f},
        {.name = "Drive",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 20.0f,
         .defaultValue = 1.0f},
    };
}

const CompiledPluginSpec& getMagdaSnareSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaSnareCompiledPlugin::xmlTypeName,
        .displayName = "Snare",
        .browserCategory = "Drums",
        .description = "Synthetic snare in three layers: a noise Transient (stick crack), a tuned "
                       "pitch-snap Body that auto-ducks under the transient, and a "
                       "resonant-high-passed noise Rattle/tail with drive. Knob-tuned, MIDI-gated "
                       "- drop it on a DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaSnareCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
