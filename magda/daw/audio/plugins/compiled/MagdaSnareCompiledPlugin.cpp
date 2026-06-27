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
        // Transient (pitched sine sweep + noise blend)
        {.name = "transient_level",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.5f},
        {.name = "transient_pitch",
         .scale = ParameterScale::Linear,
         .minValue = 100.0f,
         .maxValue = 2000.0f,
         .defaultValue = 400.0f},
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
         .defaultValue = 10.0f},
        {.name = "transient_tone",
         .scale = ParameterScale::Linear,
         .minValue = 1000.0f,
         .maxValue = 12000.0f,
         .defaultValue = 4000.0f},
        // Body
        {.name = "body_tune",
         .scale = ParameterScale::Linear,
         .minValue = 100.0f,
         .maxValue = 400.0f,
         .defaultValue = 180.0f},
        {.name = "body_snap",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.25f},
        {.name = "body_snap_time",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 2.0f,
         .maxValue = 80.0f,
         .defaultValue = 12.0f},
        {.name = "body_attack",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 100.0f,
         .defaultValue = 0.0f},
        {.name = "body_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 1500.0f,
         .defaultValue = 180.0f},
        // Rattle / tail
        {.name = "rattle_snappy",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.6f},
        {.name = "rattle_tone",
         .scale = ParameterScale::Linear,
         .minValue = 800.0f,
         .maxValue = 12000.0f,
         .defaultValue = 3000.0f},
        {.name = "rattle_hp_freq",
         .scale = ParameterScale::Linear,
         .minValue = 20.0f,
         .maxValue = 6000.0f,
         .defaultValue = 300.0f},
        {.name = "rattle_hp_reso",
         .scale = ParameterScale::Linear,
         .minValue = 0.5f,
         .maxValue = 10.0f,
         .defaultValue = 0.7f},
        {.name = "rattle_decay",
         .unit = "ms",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 1500.0f,
         .defaultValue = 200.0f},
        {.name = "rattle_drive",
         .scale = ParameterScale::Linear,
         .minValue = 1.0f,
         .maxValue = 20.0f,
         .defaultValue = 1.0f},
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
        {.name = "rattle_curve",
         .scale = ParameterScale::Linear,
         .minValue = -50.0f,
         .maxValue = 50.0f,
         .defaultValue = 0.0f},
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
