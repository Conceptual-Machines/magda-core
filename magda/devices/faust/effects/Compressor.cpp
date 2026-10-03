#include "devices/faust/effects/Compressor.hpp"

#include <algorithm>
#include <cmath>
#include <magda/sdk/audio/BlockPeak.hpp>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_compressor.generated.cpp"
#include "magda_compressor_glue.generated.cpp"

namespace magda::devices::faust {

namespace {

float ampToDb(float amp) {
    return 20.0f * std::log10(std::max(amp, 1.0e-6f));
}

/// The reduction the curve view draws at @p levelDb: the dsp's static curve, read from the
/// knobs, since the dsp reports no gain signal and an enveloped meter would lag them.
float gainReductionForLevel(float levelDb, float thresholdDb, float ratio, float kneeDb) {
    ratio = std::max(1.0f, ratio);
    kneeDb = std::max(0.0f, kneeDb);

    const float over = levelDb - thresholdDb;
    float compressedOver = over;
    if (kneeDb > 0.0f) {
        const float halfKnee = kneeDb * 0.5f;
        if (over <= -halfKnee) {
            compressedOver = over;
        } else if (over >= halfKnee) {
            compressedOver = over / ratio;
        } else {
            const float x = over + halfKnee;
            compressedOver = over + (1.0f / ratio - 1.0f) * x * x / (2.0f * kneeDb);
        }
    } else if (over > 0.0f) {
        compressedOver = over / ratio;
    }

    return std::max(0.0f, over - compressedOver);
}

float peakOfChannel(const float* samples, int numSamples) {
    return sdk::peakMagnitude(samples, numSamples);
}

float peakOfChannels(const BufferView& buffer, int firstChannel, int lastChannel) {
    float peak = 0.0f;
    for (int channel = firstChannel; channel < std::min(lastChannel, buffer.numChannels());
         ++channel)
        peak = std::max(peak, peakOfChannel(buffer.channel(channel), buffer.numFrames()));
    return peak;
}

/// Peak over every channel of the key the host routed.
float peakOfSidechain(const sdk::ProcessContext& context) {
    float peak = 0.0f;
    if (!context.sidechain)
        return peak;
    for (int channel = 0; channel < context.sidechain->numChannels(); ++channel)
        peak = std::max(peak,
                        peakOfChannel(context.sidechain->channel(channel), context.numSamples()));
    return peak;
}

}  // namespace

Compressor::Compressor() {
    initEffect();
}

::dsp* Compressor::createEngineDsp(int engineIndex) const {
    switch (static_cast<CompressorEngine>(engineIndex)) {
        case CompressorEngine::Clean:
            return new MagdaCompressorDsp();
        case CompressorEngine::Glue:
            return new MagdaCompressorGlueDsp();
    }
    return nullptr;
}

std::vector<SlotInfo> Compressor::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kEngineSlot].name = "Engine";
    infos[kEngineSlot].scale = sdk::ParameterScale::Discrete;
    infos[kEngineSlot].choices = {"Clean", "Glue"};
    infos[kEngineSlot].minValue = 0.0f;
    infos[kEngineSlot].maxValue = static_cast<float>(infos[kEngineSlot].choices.size() - 1);
    infos[kEngineSlot].defaultValue = 0.0f;

    infos[kThresholdSlot] = {.name = "Threshold",
                             .unit = "dB",
                             .scale = sdk::ParameterScale::Linear,
                             .minValue = -60.0f,
                             .maxValue = 0.0f,
                             .defaultValue = -18.0f};
    infos[kRatioSlot] = {.name = "Ratio",
                         .scale = sdk::ParameterScale::Logarithmic,
                         .minValue = 1.0f,
                         .maxValue = 50.0f,
                         .defaultValue = 4.0f,
                         .scaleAnchor = 4.0f};
    infos[kAttackSlot] = {.name = "Attack",
                          .unit = "ms",
                          .scale = sdk::ParameterScale::Logarithmic,
                          .minValue = 0.1f,
                          .maxValue = 200.0f,
                          .defaultValue = 10.0f,
                          .scaleAnchor = 10.0f};
    infos[kReleaseSlot] = {.name = "Release",
                           .unit = "ms",
                           .scale = sdk::ParameterScale::Logarithmic,
                           .minValue = 5.0f,
                           .maxValue = 1000.0f,
                           .defaultValue = 120.0f,
                           .scaleAnchor = 100.0f};
    infos[kKneeSlot] = {.name = "Knee",
                        .unit = "dB",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 0.0f,
                        .maxValue = 24.0f,
                        .defaultValue = 6.0f};
    infos[kMakeupSlot] = {.name = "Makeup",
                          .unit = "dB",
                          .scale = sdk::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 24.0f,
                          .defaultValue = 0.0f};
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

    infos[kDetectorSlot].name = "Detector";
    infos[kDetectorSlot].scale = sdk::ParameterScale::Discrete;
    infos[kDetectorSlot].choices = {"Peak", "RMS"};
    infos[kDetectorSlot].minValue = 0.0f;
    infos[kDetectorSlot].maxValue = 1.0f;
    infos[kDetectorSlot].defaultValue = 0.0f;

    infos[kLinkSlot] = {.name = "Link",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 1.0f};
    infos[kSidechainHpfSlot] = {.name = "SC HPF",
                                .unit = "Hz",
                                .scale = sdk::ParameterScale::Logarithmic,
                                .minValue = 20.0f,
                                .maxValue = 500.0f,
                                .defaultValue = 20.0f,
                                .scaleAnchor = 120.0f};
    infos[kFbffSlot] = {.name = "FBFF",
                        .scale = sdk::ParameterScale::Linear,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.5f};
    infos[kStyleSlot].name = "Style";
    infos[kStyleSlot].scale = sdk::ParameterScale::Discrete;
    infos[kStyleSlot].choices = {"Pre", "Post"};
    infos[kStyleSlot].minValue = 0.0f;
    infos[kStyleSlot].maxValue = 1.0f;
    infos[kStyleSlot].defaultValue = 0.0f;
    infos[kAutogainSlot].name = "Autogain";
    infos[kAutogainSlot].scale = sdk::ParameterScale::Discrete;
    infos[kAutogainSlot].choices = {"Off", "On"};
    infos[kAutogainSlot].minValue = 0.0f;
    infos[kAutogainSlot].maxValue = 1.0f;
    infos[kAutogainSlot].defaultValue = 0.0f;

    return infos;
}

void Compressor::beforeCompute(sdk::ProcessContext& context, int engineIndex) {
    const bool external = context.sidechain && context.sidechain->numChannels() > 0;

    // Tells the dsp to detect off the key. Only Clean has the zone; Glue yields null.
    if (auto* useSidechain = zoneForIdx(engineIndex, kUseSidechainHiddenSlot))
        *useSidechain = external ? 1.0f : 0.0f;

    const float inputPeak = peakOfChannels(context.audio, 0, 2);
    const float keyPeak = external ? peakOfSidechain(context) : inputPeak;

    inputPeakDb_.store(ampToDb(inputPeak), std::memory_order_relaxed);
    keyPeakDb_.store(ampToDb(keyPeak), std::memory_order_relaxed);
    usingExternalSidechain_.store(external, std::memory_order_relaxed);

    const float keyDb = ampToDb(keyPeak);
    gainReductionDb_.store(gainReductionForLevel(keyDb, slotDisplayValue(kThresholdSlot),
                                                 slotDisplayValue(kRatioSlot),
                                                 slotDisplayValue(kKneeSlot)),
                           std::memory_order_relaxed);
}

void Compressor::afterCompute(sdk::ProcessContext& context, int /*engineIndex*/) {
    outputPeakDb_.store(ampToDb(peakOfChannels(context.audio, 0, 2)), std::memory_order_relaxed);
}

}  // namespace magda::devices::faust
