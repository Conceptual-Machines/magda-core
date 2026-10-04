#include "devices/faust/effects/Flanger.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_flanger.generated.cpp"

namespace magda::devices::faust {

Flanger::Flanger() {
    initEffect();
}

::dsp* Flanger::createEngineDsp(int) const {
    return new MagdaFlangerDsp();
}

std::vector<SlotInfo> Flanger::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own menu: labels read as divisions, values are the
    // quarter-note multipliers the zone wants.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = sdk::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};

    infos[kRateSlot] = {.name = "Rate",
                        .unit = "Hz",
                        .scale = sdk::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 10.0f,
                        .defaultValue = 0.5f,
                        .scaleAnchor = 0.5f};

    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = sdk::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;

    infos[kDepthSlot] = {.name = "Depth",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};

    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = -0.95f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.0f};

    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.5f};

    infos[kWidthSlot] = {.name = "Width",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};

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
