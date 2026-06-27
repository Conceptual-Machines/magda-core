#include "plugins/compiled/MagdaHatCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_hat.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaHatCompiledPlugin::xmlTypeName = "magda_hat";

MagdaHatCompiledPlugin::MagdaHatCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaHatCompiledPlugin::getName() const {
    return "Hat";
}
juce::String MagdaHatCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaHatCompiledPlugin::getShortName(int) {
    return "Hat";
}
juce::String MagdaHatCompiledPlugin::getSelectableDescription() {
    return "Hat";
}

::dsp* MagdaHatCompiledPlugin::createVoiceDsp() const {
    return new MagdaHatDsp();
}

std::vector<MagdaHatCompiledPlugin::HostSlotInfo> MagdaHatCompiledPlugin::voiceSlotInfos() const {
    using magda::ParameterScale;
    return {
        // Ring (inharmonic additive partials)
        {.name = "ring_level",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.6f},
        {.name = "ring_pitch",
         .scale = ParameterScale::Linear,
         .minValue = 200.0f,
         .maxValue = 2000.0f,
         .defaultValue = 540.0f},
        {.name = "ring_spread",
         .scale = ParameterScale::Linear,
         .minValue = 0.5f,
         .maxValue = 2.0f,
         .defaultValue = 1.0f},
        {.name = "ring_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 10.0f,
         .maxValue = 2000.0f,
         .defaultValue = 300.0f},
        // Noise
        {.name = "noise_level",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "noise_tone",
         .scale = ParameterScale::Linear,
         .minValue = 800.0f,
         .maxValue = 18000.0f,
         .defaultValue = 8000.0f},
        {.name = "noise_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 5.0f,
         .maxValue = 2000.0f,
         .defaultValue = 100.0f},
        {.name = "ring_curve",
         .scale = ParameterScale::Linear,
         .minValue = -50.0f,
         .maxValue = 50.0f,
         .defaultValue = 0.0f},
        {.name = "noise_curve",
         .scale = ParameterScale::Linear,
         .minValue = -50.0f,
         .maxValue = 50.0f,
         .defaultValue = 0.0f},
    };
}

const CompiledPluginSpec& getMagdaHatSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaHatCompiledPlugin::xmlTypeName,
        .displayName = "Hat",
        .browserCategory = "Drums",
        .description = "Synthetic hi-hat in two layers: a metallic Ring (inharmonic additive "
                       "partials with a Spread/dissonance control) and a high-passed Noise sizzle, "
                       "each with its own level and decay. Short decays = closed, long = open. "
                       "Knob-tuned, MIDI-gated - drop it on a DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaHatCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
