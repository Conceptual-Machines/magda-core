#include "devices/faust/effects/Phaser.hpp"

#include <cmath>
#include <limits>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_phaser.generated.cpp"

namespace magda::devices::faust {

Phaser::Phaser() {
    initEffect();
}

::dsp* Phaser::createEngineDsp(int) const {
    return new MagdaPhaserDsp();
}

std::vector<SlotInfo> Phaser::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kRateSlot] = {.name = "Rate",
                        .unit = "Hz",
                        .scale = sdk::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 10.0f,
                        .defaultValue = 0.5f,
                        .scaleAnchor = 1.0f};
    infos[kDepthSlot] = {.name = "Depth",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 2.0f,
                         .defaultValue = 1.0f};
    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = -0.95f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.3f};
    infos[kStagesSlot].name = "Stages";
    infos[kStagesSlot].scale = sdk::ParameterScale::Discrete;
    infos[kStagesSlot].choices = {"2", "4", "6", "8"};
    infos[kStagesSlot].minValue = 0.0f;
    infos[kStagesSlot].maxValue = static_cast<float>(infos[kStagesSlot].choices.size() - 1);
    infos[kStagesSlot].defaultValue = 1.0f;
    infos[kMinHzSlot] = {.name = "Min Hz",
                         .unit = "Hz",
                         .scale = sdk::ParameterScale::Logarithmic,
                         .minValue = 30.0f,
                         .maxValue = 1000.0f,
                         .defaultValue = 100.0f,
                         .scaleAnchor = 200.0f};
    infos[kMaxHzSlot] = {.name = "Max Hz",
                         .unit = "Hz",
                         .scale = sdk::ParameterScale::Logarithmic,
                         .minValue = 500.0f,
                         .maxValue = 8000.0f,
                         .defaultValue = 2000.0f,
                         .scaleAnchor = 2000.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.6f};

    return infos;
}

void Phaser::writeExtraZones(int engineIndex) {
    // Min and Max are independent, so Min can pass Max. Keep a hertz between them: the dsp
    // divides by the span, and an inverted one sweeps backwards through the allpass chain.
    auto* minHz = zoneForIdx(engineIndex, kMinHzSlot);
    auto* maxHz = zoneForIdx(engineIndex, kMaxHzSlot);
    if (minHz != nullptr && maxHz != nullptr && *minHz >= *maxHz - 1.0f)
        *minHz = *maxHz - 1.0f;
}

}  // namespace magda::devices::faust
