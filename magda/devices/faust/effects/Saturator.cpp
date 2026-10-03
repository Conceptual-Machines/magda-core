#include "devices/faust/effects/Saturator.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_saturator.generated.cpp"

namespace magda::devices::faust {

Saturator::Saturator() {
    initEffect();
}

::dsp* Saturator::createEngineDsp(int) const {
    return new MagdaSaturatorDsp();
}

std::vector<SlotInfo> Saturator::slotInfos() const {
    using sdk::ParameterScale;
    return {
        // Drive (dB, linear). Smoothing happens inside the DSP.
        {.name = "Drive",
         .unit = "dB",
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
         .unit = "dB",
         .scale = ParameterScale::Linear,
         .minValue = -24.0f,
         .maxValue = 6.0f,
         .defaultValue = 0.0f},
    };
}

}  // namespace magda::devices::faust
