#include "plugins/compiled/MagdaSaturatorCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_saturator.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaSaturatorCompiledPlugin::xmlTypeName = "magda_saturator";

MagdaSaturatorCompiledPlugin::MagdaSaturatorCompiledPlugin() {
    initEffect();
}

::dsp* MagdaSaturatorCompiledPlugin::createEngineDsp(int) const {
    return new MagdaSaturatorDsp();
}

std::vector<MagdaSaturatorCompiledPlugin::HostSlotInfo> MagdaSaturatorCompiledPlugin::slotInfos()
    const {
    using magda::ParameterScale;
    return {
        // Drive (dB, linear). Smoothing happens inside the DSP.
        {.name = "Drive",
         .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 24.0f,
         .defaultValue = 0.0f},
        {.name = "Mode",
         .scale = ParameterScale::Discrete,
         .minValue = 0.0f,
         .maxValue = 5.0f,
         .defaultValue = 0.0f,
         .choices = {"Tanh", "Soft", "Hard", "Fold", "Tube", "Tape"}},
        {.name = "Bias",
         .scale = ParameterScale::Linear,
         .minValue = -1.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.0f},
        // Bipolar tilt.
        {.name = "Tone",
         .scale = ParameterScale::Linear,
         .minValue = -1.0f,
         .maxValue = 1.0f,
         .defaultValue = 0.0f},
        {.name = "Mix",
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 1.0f,
         .defaultValue = 1.0f},
        {.name = "Output",
         .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
         .scale = ParameterScale::Linear,
         .minValue = -24.0f,
         .maxValue = 6.0f,
         .defaultValue = 0.0f},
    };
}

constexpr AliasSpec kAliases[] = {
    {"drive", 0, "Drive"}, {"mode", 1, "Mode"}, {"bias", 2, "Bias"},
    {"tone", 3, "Tone"},   {"mix", 4, "Mix"},   {"output", 5, "Output"},
};

const CompiledPluginSpec& getMagdaSaturatorSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaSaturatorCompiledPlugin::xmlTypeName,
        .displayName = "Saturator",
        .browserCategory = "Distortion",
        .description =
            "Compiled Faust waveshaper with six selectable curves.\n"
            "<b>Tanh</b>: smooth hyperbolic, the classic warm saturation.\n"
            "<b>Soft</b>: gentle polynomial knee with a rolled-off top.\n"
            "<b>Hard</b>: instant clip ceiling for square-edged distortion.\n"
            "<b>Fold</b>: wavefolder, peaks reflect back for metallic overtones.\n"
            "<b>Tube</b>: asymmetric curve (1.4x positive, 1.0x negative) "
            "for valve-style even harmonics.\n"
            "<b>Tape</b>: tanh with an odd-order compression term, tape-style headroom.\n"
            "Drive pushes the input, Bias shifts the operating point, "
            "Tone tilts the post-shape EQ, Mix blends dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaSaturatorCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
