#include "plugins/compiled/MagdaClipperCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_clipper.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaClipperCompiledPlugin::xmlTypeName = "magda_clipper";

MagdaClipperCompiledPlugin::MagdaClipperCompiledPlugin() {
    initEffect();
}

::dsp* MagdaClipperCompiledPlugin::createEngineDsp(int) const {
    return new MagdaClipperDsp();
}

std::vector<MagdaClipperCompiledPlugin::HostSlotInfo> MagdaClipperCompiledPlugin::slotInfos()
    const {
    using magda::ParameterScale;
    return {
        {.name = "Drive",
         .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
         .scale = ParameterScale::Linear,
         .minValue = 0.0f,
         .maxValue = 24.0f,
         .defaultValue = 0.0f},
        {.name = "Mode",
         .scale = ParameterScale::Discrete,
         .minValue = 0.0f,
         .maxValue = static_cast<float>(kModeCount - 1),
         .defaultValue = 0.0f,
         .choices = {"Hard", "Soft", "Tanh", "Hyperbolic", "Sine"}},
        {.name = "Output",
         .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
         .scale = ParameterScale::Linear,
         .minValue = -24.0f,
         .maxValue = 12.0f,
         .defaultValue = 0.0f},
    };
}

void MagdaClipperCompiledPlugin::beforeCompute(DeviceProcessContext& context, int engineIndex) {
    // Pre-DSP peak, for the dot that rides the transfer curve. Read off the
    // channels the engine is about to consume rather than the whole buffer, so
    // a host block wider than the dsp does not report a peak the curve never
    // sees.
    const int channels = std::min(context.audio->getNumChannels(), engineInputCount(engineIndex));

    float peak = 0.0f;
    for (int channel = 0; channel < channels; ++channel) {
        const float* samples = context.audio->getReadPointer(channel, context.startSample);
        for (int i = 0; i < context.numSamples; ++i)
            peak = std::max(peak, std::fabs(samples[i]));
    }

    inputPeakDb_.store(20.0f * std::log10(std::max(peak, 1.0e-6f)), std::memory_order_relaxed);
}

constexpr AliasSpec kAliases[] = {
    {"drive", 0, "Drive"},
    {"mode", 1, "Mode"},
    {"output", 2, "Output"},
};

const CompiledPluginSpec& getMagdaClipperSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaClipperCompiledPlugin::xmlTypeName,
        .displayName = "Clipper",
        .browserCategory = "Distortion",
        .description = "Compiled Faust antialiased clipper with five selectable static curves "
                       "from the aa.* ADAA library.\n"
                       "<b>Hard</b>: brickwall clip ceiling.\n"
                       "<b>Soft</b>: quadratic knee for warmer breakup.\n"
                       "<b>Tanh</b>: hyperbolic tube-style curve.\n"
                       "<b>Hyperbolic</b>: smooth rational saturation.\n"
                       "<b>Sine</b>: sin(atan(x)) for asymmetric, harmonically rich clipping.\n"
                       "All five are instantiated in parallel for glitch-free Mode switching. "
                       "Drive pushes the input into the curve; Output trims the result.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaClipperCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
