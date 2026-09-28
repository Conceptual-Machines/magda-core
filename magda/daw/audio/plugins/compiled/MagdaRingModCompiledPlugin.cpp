#include "plugins/compiled/MagdaRingModCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_ring_mod.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaRingModCompiledPlugin::xmlTypeName = "magda_ring_mod";

MagdaRingModCompiledPlugin::MagdaRingModCompiledPlugin() {
    initEffect();
}

::dsp* MagdaRingModCompiledPlugin::createEngineDsp(int) const {
    return new MagdaRingModDsp();
}

std::vector<MagdaRingModCompiledPlugin::HostSlotInfo> MagdaRingModCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own [style:menu{...}]: the labels
    // read as musical divisions and the values are the quarter-note
    // multipliers the zone wants, so neither list is restated here.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = magda::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};

    infos[kFrequencySlot] = {.name = "Frequency",
                             .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                             .scale = magda::ParameterScale::Logarithmic,
                             .minValue = 1.0f,
                             .maxValue = 5000.0f,
                             .defaultValue = 100.0f,
                             .scaleAnchor = 200.0f};

    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;

    infos[kShapeSlot].name = "Shape";
    infos[kShapeSlot].scale = magda::ParameterScale::Discrete;
    infos[kShapeSlot].choices = {"Sine", "Triangle", "Square"};
    infos[kShapeSlot].minValue = 0.0f;
    infos[kShapeSlot].maxValue = static_cast<float>(infos[kShapeSlot].choices.size() - 1);
    infos[kShapeSlot].defaultValue = 0.0f;

    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.5f};

    infos[kWidthSlot] = {.name = "Width",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};

    infos[kSourceSlot].name = "Source";
    infos[kSourceSlot].scale = magda::ParameterScale::Discrete;
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

constexpr AliasSpec kAliases[] = {
    {"sync", 0, "Sync"}, {"freq", 1, "Frequency"}, {"div", 2, "Division"},  {"shape", 3, "Shape"},
    {"mix", 4, "Mix"},   {"width", 5, "Width"},    {"source", 6, "Source"},
};

const CompiledPluginSpec& getMagdaRingModSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaRingModCompiledPlugin::xmlTypeName,
        .displayName = "Ring Mod",
        .browserCategory = "Modulation",
        .description =
            "Compiled Faust stereo ring modulator. Multiplies the input by an internal carrier "
            "from 1 Hz (slow tremolo) to 5 kHz (metallic clang).\n"
            "<b>Sine</b>: pure tonal carrier, cleanest sideband structure.\n"
            "<b>Triangle</b>: softer overtone series than square.\n"
            "<b>Square</b>: rich odd-harmonic spectrum, aggressive sideband stack.\n"
            "Rate runs free in Hz or locks to tempo Division. "
            "Width offsets the carrier phase per channel; Mix blends wet against dry.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaRingModCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
