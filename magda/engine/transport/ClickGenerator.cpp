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

void synthesise(juce::AudioBuffer<float>& buffer, double sampleRate, double frequency) {
    const auto length = buffer.getNumSamples();
    const auto attack = std::max(1, static_cast<int>(kAttackSeconds * sampleRate));
    auto* samples = buffer.getWritePointer(0);

    for (auto i = 0; i < length; ++i) {
        const auto seconds = static_cast<double>(i) / sampleRate;
        const auto decay = std::exp(-seconds / kDecaySeconds);
        const auto onset = i < attack ? 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi *
                                                             static_cast<double>(i) / attack)
                                      : 1.0;

        samples[i] = static_cast<float>(
            std::sin(juce::MathConstants<double>::twoPi * frequency * seconds) * decay * onset);
    }
}

}  // namespace

void ClickGenerator::prepare(const RenderContext& context) {
    const auto length =
        std::max(1, static_cast<int>(std::lround(kClickSeconds * context.sampleRate)));

    barClick_.setSize(1, length);
    beatClick_.setSize(1, length);
    synthesise(barClick_, context.sampleRate, kBarFrequency);
    synthesise(beatClick_, context.sampleRate, kBeatFrequency);

    sounding_ = nullptr;
    soundingPosition_ = 0;
}

void ClickGenerator::trigger(bool accent) {
    sounding_ = accent ? &barClick_ : &beatClick_;
    soundingPosition_ = 0;
}

void ClickGenerator::pour(juce::AudioBuffer<float>& output, int startSample, int numSamples,
                          float gain) {
    if (sounding_ == nullptr || numSamples <= 0)
        return;

    const auto count = std::min({sounding_->getNumSamples() - soundingPosition_, numSamples,
                                 output.getNumSamples() - startSample});
    if (count <= 0) {
        sounding_ = nullptr;
        return;
    }

    for (auto channel = 0; channel < output.getNumChannels(); ++channel)
        output.addFrom(channel, startSample, *sounding_, 0, soundingPosition_, count, gain);

    soundingPosition_ += count;
    if (soundingPosition_ >= sounding_->getNumSamples())
        sounding_ = nullptr;
}

void ClickGenerator::render(const TempoMap& tempo, const ClickSettings& click,
                            const BlockInfo& block, bool countingIn,
                            juce::AudioBuffer<float>& output, int startSample) {
    if (barClick_.getNumSamples() == 0)
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
        const auto offset = block.sampleForBeat(tick.beat);
        trigger(click.emphasiseBars && tick.startsBar);
        pour(output, startSample + offset, block.numSamples - offset, click.gain);
    }
}

}  // namespace magda::engine
