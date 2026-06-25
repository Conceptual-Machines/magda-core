#include "plugins/compiled/MagdaFMCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_fm.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaFMCompiledPlugin::xmlTypeName = "magda_fm";

MagdaFMCompiledPlugin::MagdaFMCompiledPlugin(const te::PluginCreationInfo& info)
    : MagdaCompiledPolyInstrument(info) {
    initInstrument();
}

juce::String MagdaFMCompiledPlugin::getName() const {
    return "FM0";
}
juce::String MagdaFMCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaFMCompiledPlugin::getShortName(int) {
    return "FM0";
}
juce::String MagdaFMCompiledPlugin::getSelectableDescription() {
    return "FM0";
}

::dsp* MagdaFMCompiledPlugin::createVoiceDsp() const {
    return new MagdaFMDsp();
}

std::vector<MagdaFMCompiledPlugin::HostSlotInfo> MagdaFMCompiledPlugin::voiceSlotInfos() const {
    using magda::ParameterScale;
    std::vector<HostSlotInfo> v;
    v.reserve(32);

    // 0..15: 4x4 modulation matrix, row-major idx = src*4 + dst. Default patch is
    // op2 -> op1 (idx 4), a classic 2-op stack.
    for (int src = 0; src < 4; ++src)
        for (int dst = 0; dst < 4; ++dst)
            v.push_back({.name = "M" + juce::String(src + 1) + juce::String(dst + 1),
                         .scale = ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 8.0f,
                         .defaultValue = (src == 1 && dst == 0) ? 2.0f : 0.0f});

    // 16..19: per-op ratio.
    const float ratioDef[4] = {1.0f, 2.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i)
        v.push_back({.name = "Op" + juce::String(i + 1) + " Ratio",
                     .scale = ParameterScale::Linear,
                     .minValue = 0.25f,
                     .maxValue = 16.0f,
                     .defaultValue = ratioDef[i]});

    // 20..23: per-op output level in dB (only op1 reaches the output by default;
    // the rest sit at the -60 dB silent floor).
    for (int i = 0; i < 4; ++i)
        v.push_back({.name = "Op" + juce::String(i + 1) + " Level",
                     .unit = "dB",
                     .scale = ParameterScale::FaderDB,
                     .minValue = -60.0f,
                     .maxValue = 6.0f,
                     .defaultValue = (i == 0) ? 0.0f : -60.0f});

    // 24..27: amp ADSR (ms, except Sustain).
    v.push_back({.name = "Amp Attack",
                 .unit = "ms",
                 .scale = ParameterScale::Linear,
                 .minValue = 1.0f,
                 .maxValue = 2000.0f,
                 .defaultValue = 5.0f});
    v.push_back({.name = "Amp Decay",
                 .unit = "ms",
                 .scale = ParameterScale::Linear,
                 .minValue = 1.0f,
                 .maxValue = 2000.0f,
                 .defaultValue = 300.0f});
    v.push_back({.name = "Amp Sustain",
                 .scale = ParameterScale::Linear,
                 .minValue = 0.0f,
                 .maxValue = 1.0f,
                 .defaultValue = 0.5f});
    v.push_back({.name = "Amp Release",
                 .unit = "ms",
                 .scale = ParameterScale::Linear,
                 .minValue = 1.0f,
                 .maxValue = 4000.0f,
                 .defaultValue = 400.0f});

    // 28..31: per-op waveform.
    for (int i = 0; i < 4; ++i)
        v.push_back({.name = "Op" + juce::String(i + 1) + " Wave",
                     .scale = ParameterScale::Discrete,
                     .minValue = 0.0f,
                     .maxValue = 4.0f,
                     .defaultValue = 0.0f,
                     .choices = {"Sine", "Triangle", "Saw", "Square", "Noise"}});

    // 32: Glide (ms). The base forces it to 0 on the poly voices (glideVoiceSlot).
    v.push_back({.name = "Glide",
                 .unit = "ms",
                 .scale = ParameterScale::Linear,
                 .minValue = 0.0f,
                 .maxValue = 2000.0f,
                 .defaultValue = 0.0f});
    // 33: Velocity -> amplitude depth.
    v.push_back({.name = "Vel Amount",
                 .scale = ParameterScale::Linear,
                 .minValue = 0.0f,
                 .maxValue = 1.0f,
                 .defaultValue = 1.0f});
    // 34..37: per-op phase reset on note-on.
    for (int i = 0; i < 4; ++i)
        v.push_back({.name = "Op" + juce::String(i + 1) + " Reset",
                     .scale = ParameterScale::Discrete,
                     .minValue = 0.0f,
                     .maxValue = 1.0f,
                     .defaultValue = 0.0f,
                     .choices = {"Off", "On"}});

    return v;
}

const CompiledPluginSpec& getMagdaFMSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFMCompiledPlugin::xmlTypeName,
        .displayName = "FM0",
        .browserCategory = "Synth",
        .description = "Four-operator FM synth with a full 4x4 modulation matrix: every operator "
                       "(Sine/Tri/Saw/Square/Noise) can phase-modulate any operator, the diagonal "
                       "is self-feedback. Per-op ratio/level + amp ADSR. 16-voice, MIDI-driven.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaFMCompiledPlugin(info);
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
