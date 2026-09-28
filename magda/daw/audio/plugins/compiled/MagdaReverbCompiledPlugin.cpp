#include "plugins/compiled/MagdaReverbCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_reverb_hall.generated.cpp"
#include "magda_reverb_plate.generated.cpp"
#include "magda_reverb_room.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaReverbCompiledPlugin::xmlTypeName = "magda_reverb";

MagdaReverbCompiledPlugin::MagdaReverbCompiledPlugin() {
    initEffect();
}

::dsp* MagdaReverbCompiledPlugin::createEngineDsp(int engineIndex) const {
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

std::vector<MagdaReverbCompiledPlugin::HostSlotInfo> MagdaReverbCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = magda::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Plate", "Hall", "Room"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.3f};
    infos[kPredelaySlot] = {.name = "Predelay",
                            .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                            .scale = magda::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 250.0f,
                            .defaultValue = 20.0f};
    infos[kDecaySlot] = {.name = "Decay",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 100.0f,
                         .defaultValue = 50.0f};
    infos[kDampingSlot] = {.name = "Damping",
                           .scale = magda::ParameterScale::Linear,
                           .minValue = 0.0f,
                           .maxValue = 100.0f,
                           .defaultValue = 30.0f};
    infos[kLowCutSlot] = {.name = "Low Cut",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                          .scale = magda::ParameterScale::Logarithmic,
                          .minValue = 20.0f,
                          .maxValue = 500.0f,
                          .defaultValue = 40.0f,
                          .scaleAnchor = 80.0f};
    infos[kHighCutSlot] = {.name = "High Cut",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                           .scale = magda::ParameterScale::Logarithmic,
                           .minValue = 1000.0f,
                           .maxValue = 18000.0f,
                           .defaultValue = 12000.0f,
                           .scaleAnchor = 8000.0f};
    infos[kWidthSlot] = {.name = "Width",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 200.0f,
                         .defaultValue = 100.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    return infos;
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},     {"mix", 1, "Mix"},         {"predelay", 2, "Predelay"},
    {"decay", 3, "Decay"},       {"damping", 4, "Damping"}, {"low_cut", 5, "Low Cut"},
    {"high_cut", 6, "High Cut"}, {"width", 7, "Width"},     {"output", 8, "Output"},
};

// Tracktion's retired Reverb loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"reverb"};

const CompiledPluginSpec& getMagdaReverbSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaReverbCompiledPlugin::xmlTypeName,
        .displayName = "Reverb",
        .browserCategory = "Reverb",
        .description = "Compiled Faust reverb with three selectable engines.\n"
                       "<b>Plate</b>: Dattorro diffusion network for studio-plate ambience.\n"
                       "<b>Hall</b>: Zita 8-tap FDN for smooth large-space tails.\n"
                       "<b>Room</b>: Freeverb Schroeder/Moorer network for small-space ambience.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaReverbCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
