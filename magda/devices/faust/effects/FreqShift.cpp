#include "devices/faust/effects/FreqShift.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_freq_shift.generated.cpp"

namespace magda::devices::faust {

FreqShift::FreqShift() {
    initEffect();
}

::dsp* FreqShift::createEngineDsp(int) const {
    return new MagdaFreqShiftDsp();
}

std::vector<SlotInfo> FreqShift::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kShiftSlot] = {.name = "Shift",
                         .unit = "Hz",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = -1000.0f,
                         .maxValue = 1000.0f,
                         .defaultValue = 0.0f};

    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = -0.9f,
                            .maxValue = 0.9f,
                            .defaultValue = 0.0f};

    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.5f};

    infos[kSpreadSlot] = {.name = "Spread",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 0.0f};

    return infos;
}

}  // namespace magda::devices::faust
