#include "devices/faust/effects/Pitch.hpp"

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_pitch_detuner.generated.cpp"
#include "magda_pitch_harmonizer.generated.cpp"
#include "magda_pitch_shifter.generated.cpp"

namespace magda::devices::faust {

Pitch::Pitch() {
    initEffect();
}

::dsp* Pitch::createEngineDsp(int engineIndex) const {
    switch (static_cast<PitchEngine>(engineIndex)) {
        case PitchEngine::Shifter:
            return new MagdaPitchShifterDsp();
        case PitchEngine::Detuner:
            return new MagdaPitchDetunerDsp();
        case PitchEngine::Harmonizer:
            return new MagdaPitchHarmonizerDsp();
    }
    return nullptr;
}

std::vector<SlotInfo> Pitch::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = sdk::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Shifter", "Detuner", "Harmonizer"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kPitchSlot] = {.name = "Pitch",
                         .unit = "st",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = -24.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kFineSlot] = {.name = "Fine",
                        .unit = "cents",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = -100.0f,
                        .maxValue = 100.0f,
                        .defaultValue = 0.0f};
    infos[kTextureSlot] = {.name = "Texture",
                           .unit = "ms",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 8.0f,
                           .maxValue = 200.0f,
                           .defaultValue = 50.0f,
                           .scaleAnchor = 50.0f};
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
