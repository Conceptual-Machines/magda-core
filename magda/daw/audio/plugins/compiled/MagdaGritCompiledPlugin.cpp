#include "plugins/compiled/MagdaGritCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_grit.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaGritCompiledPlugin::xmlTypeName = "magda_grit";

MagdaGritCompiledPlugin::MagdaGritCompiledPlugin() {
    initEffect();
}

::dsp* MagdaGritCompiledPlugin::createEngineDsp(int) const {
    return new MagdaGritDsp();
}

std::vector<MagdaGritCompiledPlugin::HostSlotInfo> MagdaGritCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // Slot 0: Frequency (log Hz, anchored at 1 kHz so the slider mid lands
    // on the most musically useful range).
    infos[kFrequencySlot] = {.name = "Frequency",
                             .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                             .scale = magda::ParameterScale::Logarithmic,
                             .minValue = 20.0f,
                             .maxValue = 16000.0f,
                             .defaultValue = 1000.0f,
                             .scaleAnchor = 1000.0f};
    // Slot 1: Width (linear 0..1; mapped inside the DSP to Q ≈ 0.5..20).
    infos[kWidthSlot] = {.name = "Width",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};
    // Slot 2: Amount (modulation depth, 0..1).
    infos[kAmountSlot] = {.name = "Amount",
                          .scale = magda::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 0.0f};
    // Slot 3: Mode (3 carrier sources).
    infos[kModeSlot].name = "Mode";
    infos[kModeSlot].scale = magda::ParameterScale::Discrete;
    infos[kModeSlot].choices = {"Noise", "Wide Noise", "Sine"};
    infos[kModeSlot].minValue = 0.0f;
    infos[kModeSlot].maxValue = static_cast<float>(infos[kModeSlot].choices.size() - 1);
    infos[kModeSlot].defaultValue = 0.0f;

    return infos;
}

constexpr AliasSpec kAliases[] = {
    {"frequency", 0, "Frequency"},
    {"width", 1, "Width"},
    {"amount", 2, "Amount"},
    {"mode", 3, "Mode"},
};

const CompiledPluginSpec& getMagdaGritSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGritCompiledPlugin::xmlTypeName,
        .displayName = "Grit",
        .browserCategory = "Distortion",
        .description =
            "Compiled Faust texture generator. Ring-modulates the input with a tone "
            "or filtered-noise carrier for Erosion-style grit.\n"
            "<b>Noise</b>: shared mono bandpass-filtered noise on both channels.\n"
            "<b>Wide Noise</b>: decorrelated stereo noise for spatial texture.\n"
            "<b>Sine</b>: tonal sine carrier at the Frequency knob for metallic ring-mod.\n"
            "Frequency is the carrier centre (or BPF centre in the noise modes); "
            "Width sets the bandpass Q in Noise and Wide Noise modes; it has no effect "
            "in Sine mode. Amount blends the wet against the dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaGritCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
