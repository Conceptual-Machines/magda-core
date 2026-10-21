#include "devices/faust/effects/Filter.hpp"

#include <algorithm>
#include <cmath>

#include "devices/faust/effects/FilterEngines.hpp"

namespace magda::devices::faust {

Filter::Filter() {
    initEffect();
}

::dsp* Filter::createEngineDsp(int engineIndex) const {
    return createFilterDsp(engineIndex);
}

int Filter::activeEngine() const {
    const auto family = static_cast<FilterFamily>(selectedEngine());
    // As writeZones() reads a menu slot: its position over the engine's own mode count.
    const auto step = [this](int slot, int count) {
        return static_cast<int>(std::lround(parameterValue(slot) * static_cast<float>(count - 1)));
    };
    return filterDspFor(family, step(kModeSlot, filterModeCount(family)), step(kPolesSlot, 3));
}

std::vector<SlotInfo> Filter::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kCutoffSlot] = {.name = "Cutoff",
                          .unit = "Hz",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 20.0f,
                          .maxValue = 20000.0f,
                          .defaultValue = 1000.0f,
                          .scaleAnchor = 1000.0f};
    infos[kResonanceSlot] = {.name = "Resonance",
                             .scale = sdk::ParameterScale::Linear,
                             .minValue = 0.0f,
                             .maxValue = 1.0f,
                             .defaultValue = 0.0f};
    infos[kDriveSlot] = {.name = "Drive",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = sdk::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"SVF",        "Ladder", "Korg 35", "Oberheim",
                                  "Sallen-Key", "Diode",  "Comb"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;
    // The full set; the active engine narrows it at runtime.
    infos[kModeSlot].name = "Mode";
    infos[kModeSlot].scale = sdk::ParameterScale::Discrete;
    infos[kModeSlot].choices = {"LP", "BP", "HP", "Notch", "Notch+"};
    infos[kModeSlot].minValue = 0.0f;
    infos[kModeSlot].maxValue = static_cast<float>(infos[kModeSlot].choices.size() - 1);
    infos[kModeSlot].defaultValue = 0.0f;
    // Blends a post-filter soft limiter into the active engine.
    infos[kLimitSlot] = {.name = "Limit",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    infos[kMixSlot] = {.name = "Mix",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = 0.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 1.0f};
    infos[kPolesSlot].name = "Poles";
    infos[kPolesSlot].scale = sdk::ParameterScale::Discrete;
    infos[kPolesSlot].choices = {"2", "4", "6"};
    infos[kPolesSlot].minValue = 0.0f;
    infos[kPolesSlot].maxValue = 2.0f;
    infos[kPolesSlot].defaultValue = 0.0f;

    return infos;
}

int Filter::slotForDspIdx(int idx) const {
    // The dsps number Cutoff / Resonance / Drive / Mode 0..3 and Poles 5; Engine sits between
    // Drive and Mode on the panel, and neither it nor Limit has a dsp zone. Smoothing (4) stays at
    // its default here.
    switch (idx) {
        case 0:
            return kCutoffSlot;
        case 1:
            return kResonanceSlot;
        case 2:
            return kDriveSlot;
        case 3:
            return kModeSlot;
        case 5:
            return kPolesSlot;
        default:
            return -1;
    }
}

std::vector<std::string> Filter::modeChoicesForEngine(int engineIndex) {
    switch (static_cast<FilterFamily>(engineIndex)) {
        case FilterFamily::SVF:
            return {"LP", "BP", "HP", "Notch", "Notch+"};
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
        case FilterFamily::Comb:
            return {"+", "-"};
    }
    return {"LP"};
}

void Filter::writeExtraZones(int engineIndex) {
    if (engineIndex != static_cast<int>(FilterFamily::SallenKey))
        return;

    if (auto* cutoff = zoneForIdx(engineIndex, 0))
        *cutoff =
            clampSallenKeyCutoffHz(*cutoff, currentSampleRate(), slotInfo(kCutoffSlot).minValue);
}

void Filter::onPrepare(double, int maximumBlockSize) {
    dryFrames_ = std::max(0, maximumBlockSize);
    dry_.assign(static_cast<size_t>(dryFrames_) * kMaxDryChannels, 0.0f);
}

void Filter::beforeCompute(sdk::ProcessContext& context, int) {
    preSpectrumTap_.writeDownmix(context.audio);
    const int frames = std::min(context.numSamples(), dryFrames_);
    const int channels = std::min(context.audio.numChannels(), kMaxDryChannels);
    for (int channel = 0; channel < channels; ++channel)
        std::copy_n(context.audio.channel(channel), frames,
                    dry_.begin() + static_cast<std::ptrdiff_t>(channel) * dryFrames_);
}

void Filter::afterCompute(sdk::ProcessContext& context, int engineIndex) {
    // Resonance peaks are what it tames, so it runs after the filter and has no dsp zone.
    const float limitMix = std::clamp(parameterValue(kLimitSlot), 0.0f, 1.0f);
    if (limitMix > 0.0f) {
        const int channels = std::min(context.audio.numChannels(), engineOutputCount(engineIndex));
        for (int channel = 0; channel < channels; ++channel) {
            float* out = context.audio.channel(channel);
            for (int i = 0; i < context.numSamples(); ++i) {
                const float sample = out[i];
                out[i] = sanitise(sample + (std::tanh(sample) - sample) * limitMix);
            }
        }
    }

    const float mix = std::clamp(parameterValue(kMixSlot), 0.0f, 1.0f);
    if (mix < 1.0f) {
        const int frames = std::min(context.numSamples(), dryFrames_);
        const int channels = std::min(context.audio.numChannels(), kMaxDryChannels);
        for (int channel = 0; channel < channels; ++channel) {
            float* out = context.audio.channel(channel);
            const float* dry = dry_.data() + static_cast<std::ptrdiff_t>(channel) * dryFrames_;
            for (int i = 0; i < frames; ++i)
                out[i] = dry[i] + (out[i] - dry[i]) * mix;
        }
    }
    postSpectrumTap_.writeDownmix(context.audio);
}

}  // namespace magda::devices::faust
