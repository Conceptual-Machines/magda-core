#include "devices/faust/effects/Mod.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_mod.generated.cpp"

namespace magda::devices::faust {

Mod::Mod() {
    initEffect();
}

::dsp* Mod::createEngineDsp(int) const {
    return new MagdaModDsp();
}

std::vector<SlotInfo> Mod::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own menu: labels read as divisions, values are the
    // quarter-note multipliers the zone wants.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kModeSlot].name = "Mode";
    infos[kModeSlot].scale = sdk::ParameterScale::Discrete;
    infos[kModeSlot].choices = {"Tremolo", "Vibrato", "Autopan"};
    infos[kModeSlot].minValue = 0.0f;
    infos[kModeSlot].maxValue = static_cast<float>(infos[kModeSlot].choices.size() - 1);
    infos[kModeSlot].defaultValue = 0.0f;

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
                        .maxValue = 20.0f,
                        .defaultValue = 4.0f,
                        .scaleAnchor = 4.0f};

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

    infos[kShapeSlot].name = "Shape";
    infos[kShapeSlot].scale = sdk::ParameterScale::Discrete;
    infos[kShapeSlot].choices = {"Sine", "Triangle", "Square", "S&H"};
    infos[kShapeSlot].minValue = 0.0f;
    infos[kShapeSlot].maxValue = static_cast<float>(infos[kShapeSlot].choices.size() - 1);
    infos[kShapeSlot].defaultValue = 0.0f;

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
