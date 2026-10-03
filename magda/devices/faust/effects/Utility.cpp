#include "devices/faust/effects/Utility.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace magda::devices::faust {

namespace {

float onePoleAlpha(float cutoffHz, double sampleRate) {
    const float sr = static_cast<float>(std::max(1.0, sampleRate));
    const float cutoff = std::clamp(cutoffHz, 20.0f, sr * 0.45f);
    return 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * cutoff / sr);
}

}  // namespace

Utility::Utility() {
    initEffect();
}

std::vector<SlotInfo> Utility::slotInfos() const {
    std::vector<SlotInfo> infos(kHostSlotCount);
    infos[kGainSlot] = {.name = "Gain",
                        .unit = "dB",
                        .scale = sdk::ParameterScale::FaderDB,
                        .minValue = -60.0f,
                        .maxValue = 12.0f,
                        .defaultValue = 0.0f};
    infos[kPanSlot] = {.name = "Pan",
                       .scale = sdk::ParameterScale::Linear,
                       .minValue = -1.0f,
                       .maxValue = 1.0f,
                       .defaultValue = 0.0f};
    infos[kWidthSlot] = {.name = "Width",
                         .unit = "%",
                         .scale = sdk::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 200.0f,
                         .defaultValue = 100.0f};
    infos[kLowMonoFreqSlot] = {.name = "Low Mono Freq",
                               .unit = "Hz",
                               .scale = sdk::ParameterScale::Logarithmic,
                               .minValue = 20.0f,
                               .maxValue = 500.0f,
                               .defaultValue = 120.0f,
                               .scaleAnchor = 120.0f};
    infos[kMonoSlot] = {.name = "Mono",
                        .scale = sdk::ParameterScale::Boolean,
                        .minValue = 0.0f,
                        .maxValue = 1.0f,
                        .defaultValue = 0.0f};
    infos[kLowMonoSlot] = {.name = "Low Mono",
                           .scale = sdk::ParameterScale::Boolean,
                           .minValue = 0.0f,
                           .maxValue = 1.0f,
                           .defaultValue = 0.0f};
    infos[kFlipLSlot] = {.name = "Flip L",
                         .scale = sdk::ParameterScale::Boolean,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};
    infos[kFlipRSlot] = {.name = "Flip R",
                         .scale = sdk::ParameterScale::Boolean,
                         .minValue = 0.0f,
                         .maxValue = 1.0f,
                         .defaultValue = 0.0f};

    return infos;
}

void Utility::onReset() {
    lowMonoLpL1_ = 0.0f;
    lowMonoLpL2_ = 0.0f;
    lowMonoLpR1_ = 0.0f;
    lowMonoLpR2_ = 0.0f;
}

void Utility::processAudio(sdk::ProcessContext& context) {
    // No Faust engine: gain, pan, width and the Low Mono fold are the whole block.
    const int numSamples = context.numSamples();
    const int hostChannels = context.audio.numChannels();
    if (hostChannels <= 0)
        return;

    const float gainDb = slotDisplayValue(kGainSlot);
    const float gain =
        gainDb <= -59.99f ? 0.0f : (gainDb > -100.0f ? std::pow(10.0f, gainDb * 0.05f) : 0.0f);
    const float pan = std::clamp(slotDisplayValue(kPanSlot), -1.0f, 1.0f);
    const float width = std::clamp(slotDisplayValue(kWidthSlot), 0.0f, 200.0f) * 0.01f;
    const bool mono = slotDisplayValue(kMonoSlot) >= 0.5f;
    const bool lowMono = slotDisplayValue(kLowMonoSlot) >= 0.5f && !mono;
    const float flipL = slotDisplayValue(kFlipLSlot) >= 0.5f ? -1.0f : 1.0f;
    const float flipR = slotDisplayValue(kFlipRSlot) >= 0.5f ? -1.0f : 1.0f;
    const float panGainL = pan <= 0.0f ? 1.0f : 1.0f - pan;
    const float panGainR = pan >= 0.0f ? 1.0f : 1.0f + pan;
    const float lowMonoAlpha =
        onePoleAlpha(slotDisplayValue(kLowMonoFreqSlot), currentSampleRate());

    float* left = context.audio.channel(0);
    float* right = hostChannels > 1 ? context.audio.channel(1) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        float l = left[i] * flipL * gain;
        float r = (right != nullptr ? right[i] : left[i]) * flipR * gain;

        const float mid = 0.5f * (l + r);
        const float side = 0.5f * (l - r);
        l = mid + side * width;
        r = mid - side * width;

        if (mono) {
            l = mid;
            r = mid;
        } else if (lowMono) {
            lowMonoLpL1_ += lowMonoAlpha * (l - lowMonoLpL1_);
            lowMonoLpL2_ += lowMonoAlpha * (lowMonoLpL1_ - lowMonoLpL2_);
            lowMonoLpR1_ += lowMonoAlpha * (r - lowMonoLpR1_);
            lowMonoLpR2_ += lowMonoAlpha * (lowMonoLpR1_ - lowMonoLpR2_);

            const float lowL = lowMonoLpL2_;
            const float lowR = lowMonoLpR2_;
            const float lowMid = 0.5f * (lowL + lowR);
            l = lowMid + (l - lowL);
            r = lowMid + (r - lowR);
        }

        l *= panGainL;
        r *= panGainR;

        left[i] = sanitise(l);
        if (right != nullptr)
            right[i] = sanitise(r);
    }

    // Channels past the stereo pair take the gain trim only: there is no image to shape.
    for (int channel = 2; channel < hostChannels; ++channel) {
        float* out = context.audio.channel(channel);
        for (int i = 0; i < numSamples; ++i)
            out[i] = sanitise(out[i] * gain);
    }
}

}  // namespace magda::devices::faust
