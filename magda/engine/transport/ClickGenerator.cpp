#include "transport/ClickGenerator.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {
namespace {

/// A metronome is a transient, not a note: long enough to be heard over a
/// dense mix, short enough that two of them at 300 bpm do not overlap.
constexpr double kClickSeconds = 0.04;
constexpr double kDecaySeconds = 0.008;

/// Long enough to keep the click's own onset from being a click.
constexpr double kAttackSeconds = 0.001;

constexpr double kBarFrequency = 2000.0;
constexpr double kBeatFrequency = 1000.0;

/// The click @p seconds after its tick, silent before it and after it ends.
float clickAt(double seconds, double frequency) {
    if (seconds < 0.0 || seconds >= kClickSeconds)
        return 0.0f;

    const auto decay = std::exp(-seconds / kDecaySeconds);
    const auto onset =
        seconds < kAttackSeconds
            ? 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * seconds / kAttackSeconds)
            : 1.0;

    return static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * frequency * seconds) *
                              decay * onset);
}

}  // namespace

void ClickGenerator::prepare(const RenderContext& context) {
    sampleRate_ = context.sampleRate;

    // The most one pour can write: a whole click.
    scratch_.setSize(1, static_cast<int>(std::ceil(kClickSeconds * sampleRate_)) + 1);

    frequency_ = 0.0;
    elapsed_ = 0.0;
}

void ClickGenerator::trigger(bool accent, double fraction) {
    frequency_ = accent ? kBarFrequency : kBeatFrequency;
    elapsed_ = -fraction;
}

void ClickGenerator::pour(juce::AudioBuffer<float>& output, int startSample, int numSamples,
                          float gain) {
    if (frequency_ <= 0.0 || numSamples <= 0)
        return;

    const auto remaining = static_cast<int>(std::ceil(kClickSeconds * sampleRate_ - elapsed_));
    const auto count = std::min(
        {remaining, numSamples, output.getNumSamples() - startSample, scratch_.getNumSamples()});
    if (count <= 0) {
        frequency_ = 0.0;
        return;
    }

    auto* samples = scratch_.getWritePointer(0);
    for (auto i = 0; i < count; ++i)
        samples[i] = clickAt((elapsed_ + i) / sampleRate_, frequency_);

    for (auto channel = 0; channel < output.getNumChannels(); ++channel)
        output.addFrom(channel, startSample, scratch_, 0, 0, count, gain);

    elapsed_ += count;
    if (count == remaining)
        frequency_ = 0.0;
}

void ClickGenerator::render(const TempoMap& tempo, const ClickSettings& click,
                            const BlockInfo& block, bool countingIn,
                            juce::AudioBuffer<float>& output, int startSample) {
    if (!(sampleRate_ > 0.0))
        return;

    // Whatever is still sounding finishes, even if the metronome was switched
    // off while it was: cutting a decaying blip halfway through is itself a
    // click, and the switch is a setting rather than a panic.
    pour(output, startSample, block.numSamples, click.gain);

    if (!click.enabled && !countingIn)
        return;

    // A stopped block covers no beats, so this walks nothing and the metronome
    // falls silent without being told to.
    for (auto tick = tempo.tickAtOrAfter(block.beats.start); tick.beat < block.beats.end;
         tick = tempo.tickAtOrAfter(tick.nextBeat)) {
        const auto offset = block.eventForBeat(tick.beat);
        trigger(click.emphasiseBars && tick.startsBar, offset.fraction);
        pour(output, startSample + offset.value, block.numSamples - offset.value, click.gain);
    }
}

}  // namespace magda::engine
