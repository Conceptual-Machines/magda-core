#include "plugins/compiled/MagdaModCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_mod.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaModCompiledPlugin::xmlTypeName = "magda_mod";

MagdaModCompiledPlugin::MagdaModCompiledPlugin() {
    initEffect();
}

::dsp* MagdaModCompiledPlugin::createEngineDsp(int) const {
    return new MagdaModDsp();
}

std::vector<MagdaModCompiledPlugin::HostSlotInfo> MagdaModCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own [style:menu{...}]: the labels
    // read as musical divisions and the values are the quarter-note
    // multipliers the zone wants, so neither list is restated here.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    // Slot 0: Mode (Trem / Vibrato / Autopan).
    infos[kModeSlot].name = "Mode";
    infos[kModeSlot].scale = magda::ParameterScale::Discrete;
    infos[kModeSlot].choices = {"Tremolo", "Vibrato", "Autopan"};
    infos[kModeSlot].minValue = 0.0f;
    infos[kModeSlot].maxValue = static_cast<float>(infos[kModeSlot].choices.size() - 1);
    infos[kModeSlot].defaultValue = 0.0f;

    // Slot 1: Sync (Off / On).
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = magda::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};

    // Slot 2: Rate (Hz, log).
    infos[kRateSlot] = {.name = "Rate",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 0.05f,
                        .maxValue = 20.0f,
                        .defaultValue = 4.0f,
                        .scaleAnchor = 4.0f};

    // Slot 3: Division (populated from harvest).
    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;
    infos[kDivisionSlot].defaultValue = 0.0f;

    // Slot 4: Depth.
    infos[kDepthSlot] = {.name = "Depth",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.5f};

    // Slot 5: Shape.
    infos[kShapeSlot].name = "Shape";
    infos[kShapeSlot].scale = magda::ParameterScale::Discrete;
    infos[kShapeSlot].choices = {"Sine", "Triangle", "Square", "S&H"};
    infos[kShapeSlot].minValue = 0.0f;
    infos[kShapeSlot].maxValue = static_cast<float>(infos[kShapeSlot].choices.size() - 1);
    infos[kShapeSlot].defaultValue = 0.0f;

    if (!divisionValues.empty()) {
        const int n = static_cast<int>(divisionValues.size());
        infos[kDivisionSlot].choices = menuLabelsForIdx(kDivisionSlot);
        infos[kDivisionSlot].maxValue = static_cast<float>(n - 1);
        // Default to "1/4" (Faust value 1.0) if it's in the set.
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
    {"mode", 0, "Mode"},    {"sync", 1, "Sync"},   {"rate", 2, "Rate"},
    {"div", 3, "Division"}, {"depth", 4, "Depth"}, {"shape", 5, "Shape"},
};

const CompiledPluginSpec& getMagdaModSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaModCompiledPlugin::xmlTypeName,
        .displayName = "Mod",
        .browserCategory = "Modulation",
        .description = "Compiled Faust modulation effect with a shared LFO.\n"
                       "<b>Tremolo</b>: amplitude modulation, equal on both channels.\n"
                       "<b>Vibrato</b>: pitch modulation via short modulated delay.\n"
                       "<b>Autopan</b>: equal-power pan between L and R.\n"
                       "All three mode bodies run in parallel for glitch-free switching. "
                       "LFO Shape selects Sine, Triangle, Square or Sample-and-hold; "
                       "Rate runs free in Hz or locks to tempo Division.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaModCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
