#include "plugins/compiled/MagdaDelayCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_delay.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaDelayCompiledPlugin::xmlTypeName = "magda_delay";

MagdaDelayCompiledPlugin::MagdaDelayCompiledPlugin() {
    initEffect();
}

::dsp* MagdaDelayCompiledPlugin::createEngineDsp(int) const {
    return new MagdaDelayDsp();
}

std::vector<MagdaDelayCompiledPlugin::HostSlotInfo> MagdaDelayCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // The Division choices are the dsp's own [style:menu{...}]: the labels
    // read as musical divisions and the values are the quarter-note
    // multipliers the zone wants, so neither list is restated here.
    const auto divisionValues = menuValuesForIdx(kDivisionSlot);
    // Slot 0: Time (ms, 1..2000). Faust smooths internally; host slot is
    // greyed when Sync is on (gateSlotIndex below).
    infos[kTimeSlot] = {.name = "Time",
                        .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                        .scale = magda::ParameterScale::Linear,
                        .minValue = 1.0f,
                        .maxValue = 2000.0f,
                        .defaultValue = 250.0f};
    // Slot 1: Division (discrete menu). Choices populated from the harvest
    // at construction; we don't hard-code names here so the [style:menu{...}]
    // in the DSP stays the source of truth.
    infos[kDivisionSlot].name = "Division";
    infos[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    infos[kDivisionSlot].minValue = 0.0f;
    infos[kDivisionSlot].maxValue = 0.0f;  // filled in below from divisionValues
    infos[kDivisionSlot].defaultValue = 0.0f;
    // Slot 2: Sync (boolean checkbox)
    infos[kSyncSlot] = {.name = "Sync",
                        .scale = magda::ParameterScale::Discrete,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f,
                        .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                        .choices = {"Off", "On"}};
    // Slot 3: Feedback (0..0.95, the DSP's safety ceiling)
    infos[kFeedbackSlot] = {.name = "Feedback",
                            .scale = magda::ParameterScale::Linear,
                            .minValue = 0.0f,
                            .maxValue = 0.95f,
                            .defaultValue = 0.45f};
    // Slot 4: Mix (0..1)
    infos[kMixSlot] = {.name = "Mix",
                       .scale = magda::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.35f};
    // Slot 5: Tone (-1..1 tilt)
    infos[kToneSlot] = {.name = "Tone",
                        .scale = magda::ParameterScale::Linear,
                        .minValue = -1.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f};
    // Slot 6: Cross (0..1 ping-pong amount)
    infos[kCrossSlot] = {.name = "Cross",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};

    // Division choice list comes from the DSP harvest. Use the Faust-side
    // labels ("1/4", "1/8.", "1/16T") rather than stringified floats so the
    // dropdown reads as musical divisions; the underlying float values stay
    // in divisionValues for the audio-side mapping in applyToBuffer.
    if (!divisionValues.empty()) {
        const int n = static_cast<int>(divisionValues.size());
        infos[kDivisionSlot].choices = menuLabelsForIdx(kDivisionSlot);
        infos[kDivisionSlot].maxValue = static_cast<float>(n - 1);
        // Default to "1/4" if it's in the set (Faust value 1.0); otherwise 0.
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
    {"time", 0, "Time"},         {"division", 1, "Division"}, {"sync", 2, "Sync"},
    {"feedback", 3, "Feedback"}, {"mix", 4, "Mix"},           {"tone", 5, "Tone"},
    {"cross", 6, "Cross"},
};

// Tracktion's retired Delay loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"delay"};

const CompiledPluginSpec& getMagdaDelaySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaDelayCompiledPlugin::xmlTypeName,
        .displayName = "Delay",
        .browserCategory = "Delay",
        .description = "Compiled Faust stereo digital delay with fractional-sample interpolation. "
                       "Time spans 1 ms to 2 s; Sync locks to musical Division. "
                       "Feedback recirculates with Tone shaping the regen path. "
                       "Cross routes feedback across channels for ping-pong patterns.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaDelayCompiledPlugin>();
        },
        .aliasKey = "magda_delay_compiled",
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
