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
        {.name = "Tune",
         .scale = ParameterScale::Linear,
         .minValue = 50.0f,
         .maxValue = 400.0f,
         .defaultValue = 120.0f},
        {.name = "Bend",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.4f},
        {.name = "Attack",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 100.0f,
         .defaultValue = 0.0f},
        {.name = "Decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 5.0f,
         .maxValue = 2000.0f,
         .defaultValue = 400.0f},
    };
}

const CompiledPluginSpec& getMagdaTomSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaTomCompiledPlugin::xmlTypeName,
        .displayName = "Tom",
        .browserCategory = "Drums",
        .description = "Old-school drum-machine tom: a tuned sine with a downward pitch sweep "
                       "under a percussive envelope. Knob-tuned, MIDI-gated - drop it on a "
                       "DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaTomCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
