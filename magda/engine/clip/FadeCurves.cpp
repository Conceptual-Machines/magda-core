#include "clip/FadeCurves.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

constexpr float kHalfPi = 1.57079632679489661923f;
constexpr float kPi = 3.14159265358979323846f;

}  // namespace

float fadeGain(FadeCurve curve, float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    switch (curve) {
        case FadeCurve::Convex:
            return std::sin(alpha * kHalfPi);
        case FadeCurve::Concave:
            return 1.0f - std::cos(alpha * kHalfPi);
        case FadeCurve::SCurve:
            return (1.0f - alpha) * (1.0f - std::cos(alpha * kHalfPi)) +
                   alpha * std::sin(alpha * kHalfPi);
        case FadeCurve::Linear:
            break;
    }

    // Linear, and anything a project file held that is not a curve. The
    // snapshot compile already reported the latter as it substituted this.
    return alpha;
}

double fadeRampPosition(FadeCurve curve, double alpha, bool rising) {
    alpha = std::clamp(alpha, 0.0, 1.0);

    const auto pi = static_cast<double>(kPi);

    switch (curve) {
        case FadeCurve::Convex:
            return rising ? (-2.0 * std::cos((pi * alpha) / 2.0)) / pi + 1.0
                          : 1.0 - ((-2.0 * std::cos((pi * (alpha - 1.0)) / 2.0)) / pi + 1.0);

        case FadeCurve::Concave:
            return rising
                       ? alpha - (2.0 * std::sin((pi * alpha) / 2.0)) / pi + (2.0 / pi)
                       : ((2.0 * std::sin((pi * (alpha + 1.0)) / 2.0)) / pi) + alpha - (2.0 / pi);

        case FadeCurve::SCurve:
            return rising ? (alpha / 2.0) - (std::sin(pi * alpha) / (2.0 * pi)) + 0.5
                          : std::sin(pi * alpha) / (2.0 * pi) + (alpha / 2.0);

        case FadeCurve::Linear:
            break;
    }

    return rising ? (alpha * alpha * 0.5) + 0.5 : ((-(alpha - 1.0) * (alpha - 1.0)) * 0.5) + 0.5;
}

void StartDeClick::begin(juce::dsp::AudioBlock<float> audio, int fadeSamples) {
    reset();

    length_ = std::max(0, fadeSamples);
    if (length_ == 0 || audio.getNumSamples() == 0)
        return;

    // The step, read once, from the first sample of each channel. Once rather
    // than per block, because the correction that follows is a decay of THIS
    // number: re-reading it in the next block would take the step out of
    // whatever the material happened to be doing there instead.
    const auto channels = std::min(audio.getNumChannels(), kMaxChannels);
    for (std::size_t channel = 0; channel < channels; ++channel)
        offsets_[channel] = audio.getChannelPointer(channel)[0];

    applyFrom(audio, 0);
}

void StartDeClick::advance(juce::dsp::AudioBlock<float> audio) {
    if (!active())
        return;

    applyFrom(audio, done_);
}

void StartDeClick::applyFrom(juce::dsp::AudioBlock<float> audio, int alreadyDone) {
    const auto remaining = length_ - alreadyDone;
    if (remaining <= 0)
        return;

    const auto count = std::min(static_cast<std::size_t>(remaining), audio.getNumSamples());
    const auto channels = std::min(audio.getNumChannels(), kMaxChannels);

    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto discontinuity = offsets_[channel];

        // Nothing to step down from. A voice that begins on a zero crossing,
        // and every voice that begins with a fade in, lands here.
        if (discontinuity == 0.0f)
            continue;

        auto* samples = audio.getChannelPointer(channel);

        if (length_ == 1) {
            samples[0] -= discontinuity;
            continue;
        }

        for (std::size_t i = 0; i < count; ++i) {
            // Phase runs across the whole ramp rather than across this block,
            // which is the whole point of carrying the progress: the same clip
            // has to come out the same however the render was cut up.
            const auto phase = static_cast<float>(alreadyDone + static_cast<int>(i)) /
                               static_cast<float>(length_ - 1);
            const auto correction = 0.5f * (1.0f + std::cos(kPi * phase));
            samples[i] -= discontinuity * correction;
        }
    }

    done_ = alreadyDone + static_cast<int>(count);
}

}  // namespace magda::engine
