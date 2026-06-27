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
        // Transient (pitched sine sweep)
        {.name = "transient_level",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "transient_pitch",
         .scale = ParameterScale::Linear,
         .minValue = 60.0f,
         .maxValue = 1000.0f,
         .defaultValue = 220.0f},
        {.name = "transient_sweep",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "transient_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 100.0f,
         .defaultValue = 8.0f},
        // Body
        {.name = "body_pitch",
         .scale = ParameterScale::Linear,
         .minValue = 30.0f,
         .maxValue = 120.0f,
         .defaultValue = 55.0f},
        {.name = "body_snap",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "body_snap_time",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 5.0f,
         .maxValue = 1000.0f,
         .defaultValue = 60.0f},
        {.name = "body_attack",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 400.0f,
         .defaultValue = 0.0f},
        {.name = "body_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 4000.0f,
         .defaultValue = 500.0f},
        {.name = "body_drive",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 10.0f,
         .defaultValue = 2.0f},
        // Click
        {.name = "click_level",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.3f},
        {.name = "click_tone",
         .scale = ParameterScale::Linear,
         .minValue = 500.0f,
         .maxValue = 12000.0f,
         .defaultValue = 2000.0f},
        {.name = "body_curve",
         .scale = ParameterScale::Linear,
         .minValue = -50.0f,
         .maxValue = 50.0f,
         .defaultValue = 0.0f},
        {.name = "transient_curve",
         .scale = ParameterScale::Linear,
         .minValue = -50.0f,
         .maxValue = 50.0f,
         .defaultValue = 0.0f},
    };
}

const CompiledPluginSpec& getMagdaKickSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaKickCompiledPlugin::xmlTypeName,
        .displayName = "Kick",
        .browserCategory = "Drums",
        .description =
            "Synthetic kick in three layers: a Transient (pitched sine sweep), a Body "
            "(low pitch-snap sine into a saturator) that auto-ducks under the transient, "
            "and a noise Click. Knob-tuned, MIDI-gated - drop it on a DrumGrid pad or "
            "play it standalone.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaKickCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
