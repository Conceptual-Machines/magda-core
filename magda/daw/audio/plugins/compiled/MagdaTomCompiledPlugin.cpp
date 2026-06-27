#include "plugins/compiled/MagdaTomCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_tom.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaTomCompiledPlugin::xmlTypeName = "magda_tom";

MagdaTomCompiledPlugin::MagdaTomCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaTomCompiledPlugin::getName() const {
    return "Tom";
}
juce::String MagdaTomCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaTomCompiledPlugin::getShortName(int) {
    return "Tom";
}
juce::String MagdaTomCompiledPlugin::getSelectableDescription() {
    return "Tom";
}

::dsp* MagdaTomCompiledPlugin::createVoiceDsp() const {
    return new MagdaTomDsp();
}

std::vector<MagdaTomCompiledPlugin::HostSlotInfo> MagdaTomCompiledPlugin::voiceSlotInfos() const {
    using magda::ParameterScale;
    return {
        {.name = "body_tune",
         .scale = ParameterScale::Linear,
         .minValue = 50.0f,
         .maxValue = 400.0f,
         .defaultValue = 120.0f},
        {.name = "body_bend",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
        {.name = "body_attack",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 100.0f,
         .defaultValue = 0.0f},
        {.name = "body_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 5.0f,
         .maxValue = 2000.0f,
         .defaultValue = 400.0f},
        // Noise
        {.name = "noise_level",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.3f},
        {.name = "noise_tone",
         .scale = ParameterScale::Linear,
         .minValue = 200.0f,
         .maxValue = 12000.0f,
         .defaultValue = 1500.0f},
        {.name = "noise_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 5.0f,
         .maxValue = 1000.0f,
         .defaultValue = 60.0f},
        {.name = "body_curve",
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

const CompiledPluginSpec& getMagdaTomSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaTomCompiledPlugin::xmlTypeName,
        .displayName = "Tom",
        .browserCategory = "Drums",
        .description = "Synthetic tom in two layers: a tuned sine Body with a downward pitch sweep "
                       "and a high-passed Noise stick/skin attack, each with its own level and "
                       "decay. Knob-tuned, MIDI-gated - drop it on a DrumGrid pad or play it "
                       "standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaTomCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
