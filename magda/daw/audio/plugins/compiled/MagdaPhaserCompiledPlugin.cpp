#include "plugins/compiled/MagdaPhaserCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_phaser.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaPhaserCompiledPlugin::xmlTypeName = "magda_phaser";

MagdaPhaserCompiledPlugin::MagdaPhaserCompiledPlugin() {
    initEffect();
}

::dsp* MagdaPhaserCompiledPlugin::createEngineDsp(int) const {
    return new MagdaPhaserDsp();
}

std::vector<MagdaPhaserCompiledPlugin::HostSlotInfo> MagdaPhaserCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    infos[kRateSlot] = {.name = "Rate",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 10.0f,
                        .defaultValue = 0.5f,
                        .scaleAnchor = 1.0f};
    infos[kDepthSlot] = {.name = "Depth",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 2.0f,
                         .defaultValue = 1.0f};
    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = magda::ParameterScale::Linear,
                            .minValue = -0.95f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.3f};
    infos[kStagesSlot].name = "Stages";
    infos[kStagesSlot].scale = magda::ParameterScale::Discrete;
    infos[kStagesSlot].choices = {"2", "4", "6", "8"};
    infos[kStagesSlot].minValue = 0.0f;
    infos[kStagesSlot].maxValue = static_cast<float>(infos[kStagesSlot].choices.size() - 1);
    infos[kStagesSlot].defaultValue = 1.0f;
    infos[kMinHzSlot] = {.name = "Min Hz",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                         .scale = magda::ParameterScale::Logarithmic,
                         .minValue = 30.0f,
                         .maxValue = 1000.0f,
                         .defaultValue = 100.0f,
                         .scaleAnchor = 200.0f};
    infos[kMaxHzSlot] = {.name = "Max Hz",
                         .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                         .scale = magda::ParameterScale::Logarithmic,
                         .minValue = 500.0f,
                         .maxValue = 8000.0f,
                         .defaultValue = 2000.0f,
                         .scaleAnchor = 2000.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.6f};

    return infos;
}

void MagdaPhaserCompiledPlugin::writeExtraZones(int engineIndex) {
    // The sweep's endpoints are independent parameters, so the user can put Min
    // above Max. Keep at least a hertz between them: the dsp divides by the
    // span, and an inverted one sweeps backwards through its own allpass chain.
    auto* minHz = zoneForIdx(engineIndex, kMinHzSlot);
    auto* maxHz = zoneForIdx(engineIndex, kMaxHzSlot);
    if (minHz != nullptr && maxHz != nullptr && *minHz >= *maxHz - 1.0f)
        *minHz = *maxHz - 1.0f;
}

constexpr AliasSpec kAliases[] = {
    {"rate", 0, "Rate"},     {"depth", 1, "Depth"},   {"feedback", 2, "Feedback"},
    {"stages", 3, "Stages"}, {"min_hz", 4, "Min Hz"}, {"max_hz", 5, "Max Hz"},
    {"mix", 6, "Mix"},
};

// Tracktion's retired Phaser loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"phaser"};

const CompiledPluginSpec& getMagdaPhaserSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPhaserCompiledPlugin::xmlTypeName,
        .displayName = "Phaser",
        .browserCategory = "Modulation",
        .description = "Compiled Faust stereo phaser with a sweeping notch comb. "
                       "Stages selects 2, 4, 6, or 8 notches; all four counts are instantiated "
                       "in parallel, so switching is glitch-free. "
                       "Rate and Depth drive the sweep; Feedback intensifies the resonance; "
                       "Min Hz and Max Hz bound the sweep window. "
                       "Mix blends wet against dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaPhaserCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
