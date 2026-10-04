#include "devices/faust/effects/Grit.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_grit.generated.cpp"

namespace magda::devices::faust {

Grit::Grit() {
    initEffect();
}

::dsp* Grit::createEngineDsp(int) const {
    return new MagdaGritDsp();
}

std::vector<SlotInfo> Grit::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    // Slot 0: Frequency (log Hz, anchored at 1 kHz so the slider mid lands
    // on the most musically useful range).
    infos[kFrequencySlot] = {.name = "Frequency",
                             .unit = "Hz",
                             .scale = sdk::ParameterScale::Logarithmic,
                             .minValue = 20.0f,
                             .maxValue = 16000.0f,
                             .defaultValue = 1000.0f,
                             .scaleAnchor = 1000.0f};
    // Slot 1: Width (linear 0..1; mapped inside the DSP to Q ≈ 0.5..20).
    infos[kWidthSlot] = {.name = "Width",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};
    // Slot 2: Amount (modulation depth, 0..1).
    infos[kAmountSlot] = {.name = "Amount",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 0.0f};
    // Slot 3: Mode (3 carrier sources).
    infos[kModeSlot].name = "Mode";
    infos[kModeSlot].scale = sdk::ParameterScale::Discrete;
    infos[kModeSlot].choices = {"Noise", "Wide Noise", "Sine"};
    infos[kModeSlot].minValue = 0.0f;
    infos[kModeSlot].maxValue = static_cast<float>(infos[kModeSlot].choices.size() - 1);
    infos[kModeSlot].defaultValue = 0.0f;

    return infos;
}

}  // namespace magda::devices::faust
