#include "devices/faust/effects/Bitcrusher.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_bitcrusher.generated.cpp"

namespace magda::devices::faust {

Bitcrusher::Bitcrusher() {
    initEffect();
}

::dsp* Bitcrusher::createEngineDsp(int) const {
    return new MagdaBitcrusherDsp();
}

std::vector<SlotInfo> Bitcrusher::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kRateSlot] = {.name = "Rate",
                        .unit = "Hz",
                        .scale = sdk::ParameterScale::Logarithmic,
                        .minValue = 100.0f,
                        .maxValue = 48000.0f,
                        .defaultValue = 8000.0f,
                        .scaleAnchor = 4000.0f};
    infos[kBitsSlot] = {.name = "Bits",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 1.0f,
                        .maxValue = 16.0f,
                        .defaultValue = 8.0f};
    infos[kDriveSlot] = {.name = "Drive",
                         .unit = "dB",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kToneSlot] = {.name = "Tone",
                        .unit = "Hz",
                        .scale = sdk::ParameterScale::Logarithmic,
                        .minValue = 200.0f,
                        .maxValue = 20000.0f,
                        .defaultValue = 20000.0f,
                        .scaleAnchor = 2000.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    return infos;
}

}  // namespace magda::devices::faust
