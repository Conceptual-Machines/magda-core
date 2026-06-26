#include "plugins/compiled/MagdaKickCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_kick.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaKickCompiledPlugin::xmlTypeName = "magda_kick";

MagdaKickCompiledPlugin::MagdaKickCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaKickCompiledPlugin::getName() const {
    return "Kick";
}
juce::String MagdaKickCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaKickCompiledPlugin::getShortName(int) {
    return "Kick";
}
juce::String MagdaKickCompiledPlugin::getSelectableDescription() {
    return "Kick";
}

::dsp* MagdaKickCompiledPlugin::createVoiceDsp() const {
    return new MagdaKickDsp();
}

std::vector<MagdaKickCompiledPlugin::HostSlotInfo> MagdaKickCompiledPlugin::voiceSlotInfos() const {
    using magda::ParameterScale;
    return {
        {.name = "Pitch",
         .scale = ParameterScale::Linear,
         .minValue = 30.0f,
         .maxValue = 120.0f,
         .defaultValue = 55.0f},
        {.name = "Click",
         .scale = ParameterScale::Linear,
         .minValue = 0.005f,
         .maxValue = 1.0f,
         .defaultValue = 0.06f},
        {.name = "Attack",
         .scale = ParameterScale::Linear,
         .minValue = 0.005f,
         .maxValue = 0.4f,
         .defaultValue = 0.005f},
        {.name = "Decay",
         .scale = ParameterScale::Linear,
         .minValue = 0.005f,
         .maxValue = 4.0f,
         .defaultValue = 0.5f},
        {.name = "Drive",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 10.0f,
         .defaultValue = 2.0f},
    };
}

const CompiledPluginSpec& getMagdaKickSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaKickCompiledPlugin::xmlTypeName,
        .displayName = "Kick",
        .browserCategory = "Drums",
        .description = "Old-school drum-machine kick (808/909 lineage): a pitched sine sweep into "
                       "a saturator (Faust synths.lib). Knob-tuned, MIDI-gated - drop it on a "
                       "DrumGrid pad or play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaKickCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
