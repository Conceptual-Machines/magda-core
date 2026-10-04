#include "devices/faust/effects/Delay.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_delay.generated.cpp"

namespace magda::devices::faust {

Delay::Delay() {
    initEffect();
}

::dsp* Delay::createEngineDsp(int) const {
    return new MagdaDelayDsp();
}

std::vector<SlotInfo> Delay::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own menu: labels read as divisions, values are the
    // quarter-note multipliers the zone wants.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    // Greyed while Sync is on, through the dsp's gate.
    infos[kTimeSlot] = {.name = "Time",
                        .unit = "ms",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 1.0f,
                        .maxValue = 2000.0f,
                        .defaultValue = 250.0f};
    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = sdk::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = sdk::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};
    // 0.95 is the dsp's safety ceiling.
    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.45f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.35f};
    infos[kToneSlot] = {.name = "Tone",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = -1.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f};
    infos[kCrossSlot] = {.name = "Cross",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};

    if (!divisionValues.empty()) {
        const int n = static_cast<int>(divisionValues.size());
        infos[kDivisionSlot].choices = menuLabelsForIdx(kDivisionSlot);
        infos[kDivisionSlot].maxValue = static_cast<float>(n - 1);
        // "1/4" (Faust value 1.0) when the menu has it.
        for (int i = 0; i < n; ++i) {
            if (std::abs(divisionValues[static_cast<size_t>(i)] - 1.0f) < 1e-3f) {
                infos[kDivisionSlot].defaultValue = static_cast<float>(i);
                break;
            }
        }
    }

    return infos;
}

}  // namespace magda::devices::faust
