#include "devices/faust/effects/Reverb.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_reverb_hall.generated.cpp"
#include "magda_reverb_plate.generated.cpp"
#include "magda_reverb_room.generated.cpp"

namespace magda::devices::faust {

Reverb::Reverb() {
    initEffect();
}

::dsp* Reverb::createEngineDsp(int engineIndex) const {
    switch (static_cast<ReverbEngine>(engineIndex)) {
        case ReverbEngine::Plate:
            return new MagdaReverbPlateDsp();
        case ReverbEngine::Hall:
            return new MagdaReverbHallDsp();
        case ReverbEngine::Room:
            return new MagdaReverbRoomDsp();
    }
    return nullptr;
}

std::vector<SlotInfo> Reverb::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = sdk::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Plate", "Hall", "Room"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.3f};
    infos[kPredelaySlot] = {.name = "Predelay",
                            .unit = "ms",
                            .scale = sdk::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 250.0f,
                            .defaultValue = 20.0f};
    infos[kDecaySlot] = {.name = "Decay",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 100.0f,
                         .defaultValue = 50.0f};
    infos[kDampingSlot] = {.name = "Damping",
                           .scale = sdk::ParameterScale::Linear,
                           .minValue = 0.0f,
                           .maxValue = 100.0f,
                           .defaultValue = 30.0f};
    infos[kLowCutSlot] = {.name = "Low Cut",
                          .unit = "Hz",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 20.0f,
                          .maxValue = 500.0f,
                          .defaultValue = 40.0f,
                          .scaleAnchor = 80.0f};
    infos[kHighCutSlot] = {.name = "High Cut",
                           .unit = "Hz",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 1000.0f,
                           .maxValue = 18000.0f,
                           .defaultValue = 12000.0f,
                           .scaleAnchor = 8000.0f};
    infos[kWidthSlot] = {.name = "Width",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 200.0f,
                         .defaultValue = 100.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    return infos;
}

}  // namespace magda::devices::faust
