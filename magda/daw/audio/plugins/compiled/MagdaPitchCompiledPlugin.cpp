#include "plugins/compiled/MagdaPitchCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_pitch_detuner.generated.cpp"
#include "magda_pitch_harmonizer.generated.cpp"
#include "magda_pitch_shifter.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaPitchCompiledPlugin::xmlTypeName = "magda_pitch";

MagdaPitchCompiledPlugin::MagdaPitchCompiledPlugin() {
    initEffect();
}

::dsp* MagdaPitchCompiledPlugin::createEngineDsp(int engineIndex) const {
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

std::vector<MagdaPitchCompiledPlugin::HostSlotInfo> MagdaPitchCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = magda::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Shifter", "Detuner", "Harmonizer"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kPitchSlot] = {.name = "Pitch",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Semitones),
                         .scale = magda::ParameterScale::Linear,
                         .minValue = -24.0f,
                         .maxValue = 24.0f,
                         .defaultValue = 0.0f};
    infos[kFineSlot] = {.name = "Fine",
                        .unit = "cents",
                        .scale = magda::ParameterScale::Linear,
                        .minValue = -100.0f,
                        .maxValue = 100.0f,
                        .defaultValue = 0.0f};
    infos[kTextureSlot] = {.name = "Texture",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                           .scale = magda::ParameterScale::Logarithmic,
                           .minValue = 8.0f,
                           .maxValue = 200.0f,
                           .defaultValue = 50.0f,
                           .scaleAnchor = 50.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    infos[kOutputSlot] = {.name = "Output",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    return infos;
}

constexpr AliasSpec kAliases[] = {
    {"engine", 0, "Engine"},   {"pitch", 1, "Pitch"}, {"fine", 2, "Fine"},
    {"texture", 3, "Texture"}, {"mix", 4, "Mix"},     {"output", 5, "Output"},
};

// Tracktion's retired Pitch Shift loads here; see core/LegacyDeviceAliases.hpp.
// "pitch shift" is the retired device's display name, kept so an instruction
// that asks for one by that name still lands on this device.
constexpr const char* kLoadAliases[] = {"pitchShifter", "pitchshift", "pitch shift"};

const CompiledPluginSpec& getMagdaPitchSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPitchCompiledPlugin::xmlTypeName,
        .displayName = "Pitch",
        .browserCategory = "Pitch",
        .description = "Compiled Faust pitch shifter with three selectable engines.\n"
                       "<b>Shifter</b>: single voice, full plus/minus 24 semitones.\n"
                       "<b>Detuner</b>: two voices hard-panned L/R for chorus-style thickening.\n"
                       "<b>Harmonizer</b>: shifted voice summed with dry at a chosen interval.\n"
                       "All three use ef.transpose; transient smear and grain are by design.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaPitchCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
