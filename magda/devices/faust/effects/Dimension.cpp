#include "devices/faust/effects/Dimension.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_dimension_dim.generated.cpp"
#include "magda_dimension_haas.generated.cpp"
#include "magda_dimension_ms.generated.cpp"

namespace magda::devices::faust {

Dimension::Dimension() {
    initEffect();
}

::dsp* Dimension::createEngineDsp(int engineIndex) const {
    switch (static_cast<DimensionEngine>(engineIndex)) {
        case DimensionEngine::Dimension:
            return new MagdaDimensionDimDsp();
        case DimensionEngine::Haas:
            return new MagdaDimensionHaasDsp();
        case DimensionEngine::MidSide:
            return new MagdaDimensionMSDsp();
    }
    return nullptr;
}

std::vector<SlotInfo> Dimension::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = sdk::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Dimension", "Haas", "M/S"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kAmountSlot] = {.name = "Amount",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 0.5f};
    infos[kRateSlot] = {.name = "Rate",
                        .unit = "Hz",
                        .scale = sdk::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 4.0f,
                        .defaultValue = 0.5f,
                        .scaleAnchor = 0.5f};
    infos[kWidthSlot] = {.name = "Width",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 200.0f,
                         .defaultValue = 100.0f};
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
