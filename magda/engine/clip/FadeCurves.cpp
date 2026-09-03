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

void StopDeClick::hold(juce::dsp::AudioBlock<float> audio, int upTo) {
    if (upTo <= 0)
        return;

    const auto channels = std::min(audio.getNumChannels(), kMaxChannels);
    for (std::size_t channel = 0; channel < channels; ++channel)
        held_[channel] = audio.getChannelPointer(channel)[static_cast<std::size_t>(upTo) - 1];

    // Channels this block did not have are stale rather than silent, and a
    // stop that decayed them would add a sample the source stopped producing
    // some blocks ago. A track that narrows keeps its wider channels quiet.
    for (auto channel = channels; channel < kMaxChannels; ++channel)
        held_[channel] = 0.0f;
}

void StopDeClick::push(juce::dsp::AudioBlock<float> audio) {
    // Not while a ramp is running, and this is what keeps the ramp independent
    // of how the render was cut up. What a running ramp decays is the value the
    // signal stopped at, and the buffer it was just written into now holds that
    // value part way down. Taking the remembered sample from there would
    // multiply the rest of the cosine by an already decayed number, so the ramp
    // would bend at every block seam and a stop would come out differently at
    // 8 samples a block and at 1024.
    //
    // Refusing here rather than at the callers, because every caller pushes
    // unconditionally every block: which of those blocks a ramp happens to be
    // running through is exactly what they should not have to know.
    if (active())
        return;

    hold(audio, static_cast<int>(audio.getNumSamples()));
}

void StopDeClick::begin(juce::dsp::AudioBlock<float> audio, int offset, int fadeSamples) {
    // Whatever a previous ramp had left is finished by this one: a stop landing
    // while an earlier stop is still decaying steps down from where the signal
    // is now, not from where it was two stops ago. Deliberately past the guard
    // in push, because a new stop is the one thing that should re-anchor a
    // running ramp.
    hold(audio, offset);

    length_ = std::max(0, fadeSamples);
    done_ = 0;

    if (length_ == 0)
        return;

    applyFrom(audio, offset, 0);
}

void StopDeClick::advance(juce::dsp::AudioBlock<float> audio) {
    if (!active())
        return;

    applyFrom(audio, 0, done_);
}

void StopDeClick::applyFrom(juce::dsp::AudioBlock<float> audio, int offset, int alreadyDone) {
    const auto remaining = length_ - alreadyDone;
    if (remaining <= 0)
        return;

    const auto room = static_cast<int>(audio.getNumSamples()) - offset;
    if (room <= 0)
        return;

    const auto count = static_cast<std::size_t>(std::min(remaining, room));
    const auto channels = std::min(audio.getNumChannels(), kMaxChannels);

    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto discontinuity = held_[channel];

        // Nothing to step down from. A source that ended on a zero crossing,
        // and every source that ended through a fade out, lands here.
        if (discontinuity == 0.0f)
            continue;

        auto* samples = audio.getChannelPointer(channel) + offset;

        if (length_ == 1) {
            samples[0] += discontinuity;
            continue;
        }

        for (std::size_t i = 0; i < count; ++i) {
            // Phase runs across the whole ramp rather than across this block,
            // which is what carrying the progress is for: the same stop has to
            // come out the same however the render was cut up.
            const auto phase = static_cast<float>(alreadyDone + static_cast<int>(i)) /
                               static_cast<float>(length_ - 1);
            const auto correction = 0.5f * (1.0f + std::cos(kPi * phase));
            samples[i] += discontinuity * correction;
        }
    }

    done_ = alreadyDone + static_cast<int>(count);
}

}  // namespace magda::engine
