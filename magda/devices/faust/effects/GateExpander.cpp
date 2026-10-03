#include "devices/faust/effects/GateExpander.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_gate_expander.generated.cpp"

namespace magda::devices::faust {

GateExpander::GateExpander() {
    initEffect();
}

::dsp* GateExpander::createEngineDsp(int) const {
    return new MagdaGateExpanderDsp();
}

std::vector<SlotInfo> GateExpander::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = "ms",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 100.0f,
                          .defaultValue = 1.0f,
                          .scaleAnchor = 1.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = "ms",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 5.0f,
                           .maxValue = 1000.0f,
                           .defaultValue = 120.0f,
                           .scaleAnchor = 100.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 24.0f,
                          .defaultValue = 0.0f};
    infos[kThresholdSlot] = {.name = "Threshold",
                             .unit = "dB",
                             .scale = sdk::ParameterScale::Linear,
                             .minValue = -80.0f,
                             .maxValue = 0.0f,
                             .defaultValue = -40.0f};
    infos[kRatioSlot] = {.name = "Ratio",
                         .scale = sdk::ParameterScale::Logarithmic,
                         .minValue = 1.0f,
                         .maxValue = 50.0f,
                         .defaultValue = 4.0f,
                         .scaleAnchor = 4.0f};
    infos[kRangeSlot] = {.name = "Range",
                         .unit = "dB",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 80.0f,
                         .defaultValue = 60.0f};

    return infos;
}

}  // namespace magda::devices::faust
