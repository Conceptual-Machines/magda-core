#include "devices/faust/effects/GrainDelay.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_granular_delay.generated.cpp"

namespace magda::devices::faust {

GrainDelay::GrainDelay() {
    initEffect();
}

::dsp* GrainDelay::createEngineDsp(int) const {
    return new MagdaGrainDelayDsp();
}

std::vector<SlotInfo> GrainDelay::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own menu: labels read as divisions, values are the
    // quarter-note multipliers the zone wants.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kTimeSlot] = {.name = "Time",
                        .unit = "ms",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 1.0f,
                        .maxValue = 2000.0f,
                        .defaultValue = 500.0f};
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
    infos[kSizeSlot] = {.name = "Size",
                        .unit = "ms",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 20.0f,
                        .maxValue = 500.0f,
                        .defaultValue = 120.0f};
    infos[kPitchSlot] = {.name = "Pitch",
                         .unit = "st",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = -24.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kSpraySlot] = {.name = "Spray",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.30f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.40f};

    if (!divisionValues.empty()) {
        const int n = static_cast<int>(divisionValues.size());
        infos[kDivisionSlot].choices = menuLabelsForIdx(kDivisionSlot);
        infos[kDivisionSlot].maxValue = static_cast<float>(n - 1);
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
