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

void applyStartDeClick(juce::dsp::AudioBlock<float> audio, int fadeSamples) {
    const auto length = std::min(static_cast<std::size_t>(std::max(0, fadeSamples)),
                                 static_cast<std::size_t>(audio.getNumSamples()));
    if (length == 0)
        return;

    for (std::size_t channel = 0; channel < audio.getNumChannels(); ++channel) {
        auto* samples = audio.getChannelPointer(channel);
        const auto discontinuity = samples[0];

        // Nothing to step down from. A voice that begins on a zero crossing,
        // and every voice that begins with a fade in, lands here.
        if (discontinuity == 0.0f)
            continue;

        if (length == 1) {
            samples[0] = 0.0f;
            continue;
        }

        for (std::size_t i = 0; i < length; ++i) {
            const auto phase = static_cast<float>(i) / static_cast<float>(length - 1);
            const auto correction = 0.5f * (1.0f + std::cos(kPi * phase));
            samples[i] -= discontinuity * correction;
        }
    }
}

}  // namespace magda::engine
