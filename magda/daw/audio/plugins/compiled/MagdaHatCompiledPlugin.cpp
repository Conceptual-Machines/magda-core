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
        {.name = "Pitch",
         .scale = ParameterScale::Linear,
         .minValue = 317.0f,
         .maxValue = 3170.0f,
         .defaultValue = 800.0f},
        {.name = "Tone",
         .scale = ParameterScale::Linear,
         .minValue = 800.0f,
         .maxValue = 18000.0f,
         .defaultValue = 8000.0f},
        {.name = "Attack",
         .scale = ParameterScale::Linear,
         .minValue = 0.005f,
         .maxValue = 0.2f,
         .defaultValue = 0.005f},
        {.name = "Decay",
         .scale = ParameterScale::Linear,
         .minValue = 0.005f,
         .maxValue = 4.0f,
         .defaultValue = 0.1f},
    };
}

const CompiledPluginSpec& getMagdaHatSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaHatCompiledPlugin::xmlTypeName,
        .displayName = "Hat",
        .browserCategory = "Drums",
        .description = "Old-school drum-machine hi-hat: a phase-modulated metallic tone through a "
                       "resonant lowpass (Faust synths.lib). Short Decay = closed, long Decay = "
                       "open. Knob-tuned, MIDI-gated - drop it on a DrumGrid pad or play it "
                       "standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaHatCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
