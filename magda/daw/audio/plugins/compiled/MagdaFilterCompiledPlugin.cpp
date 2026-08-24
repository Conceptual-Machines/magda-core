#include "plugins/compiled/MagdaFilterCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_filter_diode.generated.cpp"
#include "magda_filter_korg35.generated.cpp"
#include "magda_filter_ladder.generated.cpp"
#include "magda_filter_oberheim.generated.cpp"
#include "magda_filter_sk.generated.cpp"
#include "magda_filter_svf.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaFilterCompiledPlugin::xmlTypeName = "magda_filter";

namespace {

/// Sallen-Key goes unstable as its cutoff approaches Nyquist, so it gets a
/// lower ceiling than the host slot's own range. The other five take the
/// slider's value as it stands.
float clampSallenKeyCutoffHz(float cutoffHz, double sampleRate, float minCutoffHz) {
    if (sampleRate <= 0.0)
        return cutoffHz;

    constexpr float kStableNyquistFraction = 0.84f;
    const float maxCutoffHz = static_cast<float>(sampleRate) * 0.5f * kStableNyquistFraction;
    if (maxCutoffHz <= minCutoffHz)
        return minCutoffHz;
    return juce::jlimit(minCutoffHz, maxCutoffHz, cutoffHz);
}

}  // namespace

MagdaFilterCompiledPlugin::MagdaFilterCompiledPlugin() {
    initEffect();
}

::dsp* MagdaFilterCompiledPlugin::createEngineDsp(int engineIndex) const {
    switch (static_cast<FilterFamily>(engineIndex)) {
        case FilterFamily::SVF:
            return new MagdaSVFDsp();
        case FilterFamily::Ladder:
            return new MagdaLadderDsp();
        case FilterFamily::Korg35:
            return new MagdaKorg35Dsp();
        case FilterFamily::Oberheim:
            return new MagdaOberheimDsp();
        case FilterFamily::SallenKey:
            return new MagdaSallenKeyDsp();
        case FilterFamily::Diode:
            return new MagdaDiodeDsp();
    }
    return nullptr;
}

std::vector<MagdaFilterCompiledPlugin::HostSlotInfo> MagdaFilterCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // Slot 0: Cutoff (continuous, log, anchored at 1 kHz)
    infos[kCutoffSlot] = {.name = "Cutoff",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                          .scale = magda::ParameterScale::Logarithmic,
                          .minValue = 20.0f,
                          .maxValue = 20000.0f,
                          .defaultValue = 1000.0f,
                          .scaleAnchor = 1000.0f};
    // Slot 1: Resonance (continuous, linear, 0..1)
    infos[kResonanceSlot] = {.name = "Resonance",
                             .scale = magda::ParameterScale::Linear,
                             .minValue = 0.0f,
                             .maxValue = 1.0f,
                             .defaultValue = 0.0f};
    // Slot 2: Drive (continuous, linear, 0..1)
    infos[kDriveSlot] = {.name = "Drive",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    // Slot 3: Engine (discrete, 6 options)
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = magda::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"SVF", "Ladder", "Korg 35", "Oberheim", "Sallen-Key", "Diode"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;
    // Slot 4: Mode (discrete, 4 options) — engine-specific fallback at runtime
    infos[kModeSlot].name = "Mode";
    infos[kModeSlot].scale = magda::ParameterScale::Discrete;
    infos[kModeSlot].choices = {"LP", "BP", "HP", "Notch"};
    infos[kModeSlot].minValue = 0.0f;
    infos[kModeSlot].maxValue = static_cast<float>(infos[kModeSlot].choices.size() - 1);
    infos[kModeSlot].defaultValue = 0.0f;
    // Slot 5: Limit blends a post-filter soft limiter into the active engine.
    infos[kLimitSlot] = {.name = "Limit",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};

    return infos;
}

int MagdaFilterCompiledPlugin::slotForDspIdx(int idx) const {
    // The dsps number Cutoff / Resonance / Drive / Mode 0..3. The host table
    // puts Engine between Drive and Mode, because Engine is the top-level
    // choice and belongs next to them on the panel, and no dsp has a zone for
    // it. Limit is wrapper-only for the same reason.
    switch (idx) {
        case 0:
            return kCutoffSlot;
        case 1:
            return kResonanceSlot;
        case 2:
            return kDriveSlot;
        case 3:
            return kModeSlot;
        default:
            return -1;
    }
}

std::vector<juce::String> MagdaFilterCompiledPlugin::modeChoicesForEngine(int engineIndex) const {
    switch (static_cast<FilterFamily>(engineIndex)) {
        case FilterFamily::SVF:
            return {"LP", "BP", "HP", "Notch"};
        case FilterFamily::Ladder:
            return {"LP"};
        case FilterFamily::Korg35:
            return {"LP", "HP"};
        case FilterFamily::Oberheim:
            return {"LP", "BP", "HP", "Notch"};
        case FilterFamily::SallenKey:
            return {"LP", "BP", "HP"};
        case FilterFamily::Diode:
            return {"LP"};
    }
    return {"LP"};
}

void MagdaFilterCompiledPlugin::writeExtraZones(int engineIndex) {
    if (engineIndex != static_cast<int>(FilterFamily::SallenKey))
        return;

    if (auto* cutoff = zoneForIdx(engineIndex, 0))
        *cutoff =
            clampSallenKeyCutoffHz(*cutoff, currentSampleRate(), getSlotInfo(kCutoffSlot).minValue);
}

void MagdaFilterCompiledPlugin::afterCompute(DeviceProcessContext& context, int engineIndex) {
    // Limit blends a soft limiter over whatever the active engine produced. It
    // has no zone in any dsp: resonance peaks are the thing being tamed, so it
    // belongs after the filter rather than inside it.
    const float limitMix = juce::jlimit(0.0f, 1.0f, getSlotParameter(kLimitSlot).currentValue());
    if (limitMix <= 0.0f)
        return;

    const int channels = std::min(context.audio->getNumChannels(), engineOutputCount(engineIndex));
    for (int channel = 0; channel < channels; ++channel) {
        float* out = context.audio->getWritePointer(channel, context.startSample);
        for (int i = 0; i < context.numSamples; ++i) {
            const float sample = out[i];
            out[i] = sanitise(sample + (std::tanh(sample) - sample) * limitMix);
        }
    }
}

constexpr AliasSpec kAliases[] = {
    {"cutoff", 0, "Cutoff"}, {"resonance", 1, "Resonance"}, {"drive", 2, "Drive"},
    {"engine", 3, "Engine"}, {"mode", 4, "Mode"},           {"limit", 5, "Limit"},
};

// Tracktion's retired Lowpass loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"lowpass"};

const CompiledPluginSpec& getMagdaFilterSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaFilterCompiledPlugin::xmlTypeName,
        .displayName = "Filter",
        .browserCategory = "Filter",
        .description =
            "Compiled Faust multimode filter.\n"
            "<b>SVF</b>: clean 2-pole LP/BP/HP/Notch for precise shaping.\n"
            "<b>Ladder</b>: classic 4-pole low-pass with driven resonance.\n"
            "<b>Korg 35</b>: MS-style LP/HP character with sharper analog bite.\n"
            "<b>Oberheim</b>: SEM-style LP/BP/HP/Notch with broad musical sweeps.\n"
            "<b>Sallen-Key</b>: smooth 2nd-order LP/BP/HP response.\n"
            "<b>Diode</b>: resonant 4-pole diode ladder with input drive.\n"
            "<warning>Warning: high resonance can create very loud peaks or "
            "self-oscillation. "
            "Keep monitoring levels conservative to protect speakers and ears.</warning>",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaFilterCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
