#include "devices/faust/effects/RingMod.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_ring_mod.generated.cpp"

namespace magda::devices::faust {

RingMod::RingMod() {
    initEffect();
}

::dsp* RingMod::createEngineDsp(int) const {
    return new MagdaRingModDsp();
}

std::vector<SlotInfo> RingMod::slotInfos() const {
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

    infos[kFrequencySlot] = {.name = "Frequency",
                             .unit = "Hz",
                             .scale = sdk::ParameterScale::Logarithmic,
                             .minValue = 1.0f,
                             .maxValue = 5000.0f,
                             .defaultValue = 100.0f,
                             .scaleAnchor = 200.0f};

    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = sdk::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;

    infos[kShapeSlot].name = "Shape";
    infos[kShapeSlot].scale = sdk::ParameterScale::Discrete;
    infos[kShapeSlot].choices = {"Sine", "Triangle", "Square"};
    infos[kShapeSlot].minValue = 0.0f;
    infos[kShapeSlot].maxValue = static_cast<float>(infos[kShapeSlot].choices.size() - 1);
    infos[kShapeSlot].defaultValue = 0.0f;

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

    infos[kSourceSlot].name = "Source";
    infos[kSourceSlot].scale = sdk::ParameterScale::Discrete;
    infos[kSourceSlot].choices = {"Oscillator", "Sidechain"};
    infos[kSourceSlot].minValue = 0.0f;
    infos[kSourceSlot].maxValue = static_cast<float>(infos[kSourceSlot].choices.size() - 1);
    infos[kSourceSlot].defaultValue = 0.0f;

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
