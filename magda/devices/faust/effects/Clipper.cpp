#include "devices/faust/effects/Clipper.hpp"

#include <algorithm>
#include <cmath>
#include <magda/sdk/audio/BlockPeak.hpp>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_clipper.generated.cpp"

namespace magda::devices::faust {

Clipper::Clipper() {
    initEffect();
}

::dsp* Clipper::createEngineDsp(int) const {
    return new MagdaClipperDsp();
}

std::vector<SlotInfo> Clipper::slotInfos() const {
    using sdk::ParameterScale;
    return {
        {.name = "Drive",
         .unit = "dB",
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
         .unit = "dB",
         .scale = ParameterScale::Linear,
         .minValue = -24.0f,
         .maxValue = 12.0f,
         .defaultValue = 0.0f},
    };
}

void Clipper::beforeCompute(sdk::ProcessContext& context, int engineIndex) {
    // Pre-dsp peak for the transfer-curve dot, over the channels the engine consumes.
    const int channels = std::min(context.audio.numChannels(), engineInputCount(engineIndex));

    // peakMagnitude skips NaN, which would otherwise stick in inputPeakDb_.
    float peak = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        peak = std::max(peak,
                        sdk::peakMagnitude(context.audio.channel(channel), context.numSamples()));

    inputPeakDb_.store(20.0f * std::log10(std::max(peak, 1.0e-6f)), std::memory_order_relaxed);
}

}  // namespace magda::devices::faust
