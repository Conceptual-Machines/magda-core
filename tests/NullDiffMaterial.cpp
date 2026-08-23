#include "NullDiffMaterial.hpp"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace magda::nulldiff {

namespace {

/// A 32-bit generator with no library dependency, so that the same seed gives
/// the same noise on every platform. std::mt19937 would too, but the
/// distributions on top of it are not specified to.
struct Lcg {
    explicit Lcg(unsigned int seed) : state(seed | 1u) {}

    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<double>(state) / 2147483648.0 - 1.0);
    }

    unsigned int state;
};

/// Long enough to take the click off a tone's ends and short enough not to be
/// the thing under test. Five milliseconds, in samples.
int windowSamples(double sampleRate) {
    return juce::jmax(1, static_cast<int>(0.005 * sampleRate));
}

}  // namespace

juce::AudioBuffer<float> renderMaterial(const MaterialSpec& spec) {
    const auto numSamples =
        juce::jmax(1, static_cast<int>(std::llround(spec.durationSeconds * spec.sampleRate)));
    juce::AudioBuffer<float> buffer(juce::jmax(1, spec.channels), numSamples);
    buffer.clear();

    switch (spec.kind) {
        case MaterialKind::Impulses: {
            const auto stride = juce::jmax(
                1, static_cast<int>(std::llround(spec.intervalSeconds * spec.sampleRate)));
            for (auto sample = 0; sample < numSamples; sample += stride)
                for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample(channel, sample, spec.level * 2.0f);
            break;
        }

        case MaterialKind::Steps: {
            const auto stride = juce::jmax(
                1, static_cast<int>(std::llround(spec.intervalSeconds * spec.sampleRate)));
            for (auto sample = 0; sample < numSamples; ++sample) {
                const auto sign = ((sample / stride) % 2 == 0) ? 1.0f : -1.0f;
                for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample(channel, sample, sign * spec.level);
            }
            break;
        }

        case MaterialKind::Tone: {
            const auto fade = juce::jmin(windowSamples(spec.sampleRate), numSamples / 2);
            const auto increment =
                spec.frequency * juce::MathConstants<double>::twoPi / spec.sampleRate;
            for (auto sample = 0; sample < numSamples; ++sample) {
                auto value = std::sin(increment * static_cast<double>(sample));

                // A raised cosine at each end. A rectangular window would put a
                // step at the file's edges, and a step is broadband, which is
                // the one thing this material exists not to be.
                if (fade > 0) {
                    if (sample < fade)
                        value *= 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi *
                                                      static_cast<double>(sample) /
                                                      static_cast<double>(fade));
                    else if (sample >= numSamples - fade)
                        value *= 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi *
                                                      static_cast<double>(numSamples - 1 - sample) /
                                                      static_cast<double>(fade));
                }

                for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample(channel, sample, spec.level * static_cast<float>(value));
            }
            break;
        }

        case MaterialKind::PulsedTone: {
            const auto increment =
                spec.frequency * juce::MathConstants<double>::twoPi / spec.sampleRate;
            const auto pulse = spec.pulseHz * juce::MathConstants<double>::twoPi / spec.sampleRate;

            for (auto sample = 0; sample < numSamples; ++sample) {
                const auto carrier = std::sin(increment * static_cast<double>(sample));

                // Never quite to silence: a gap would give the envelope nothing
                // to correlate through, and a stretcher fed silence is a
                // different question from a stretcher fed a swell.
                const auto envelope =
                    0.15 + 0.85 * (0.5 - 0.5 * std::cos(pulse * static_cast<double>(sample)));

                for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample(channel, sample,
                                     spec.level * static_cast<float>(carrier * envelope));
            }
            break;
        }

        case MaterialKind::Noise: {
            for (auto channel = 0; channel < buffer.getNumChannels(); ++channel) {
                Lcg random(spec.seed + static_cast<unsigned int>(channel) * 0x85EBCA6Bu);
                auto* out = buffer.getWritePointer(channel);
                for (auto sample = 0; sample < numSamples; ++sample)
                    out[sample] = spec.level * random.next();
            }
            break;
        }
    }

    return buffer;
}

juce::File writeMaterial(const juce::File& directory, const juce::String& name,
                         const MaterialSpec& spec) {
    directory.createDirectory();
    const auto file = directory.getChildFile(name + ".wav");
    file.deleteFile();

    const auto buffer = renderMaterial(spec);

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return {};

    // 32-bit float, and the whole corpus depends on it: anything narrower puts
    // quantisation noise above the null floor and every case would be measuring
    // the file format rather than the engines.
    const auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(spec.sampleRate)
            .withNumChannels(buffer.getNumChannels())
            .withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    auto writer = format.createWriterFor(stream, options);
    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    return file;
}

}  // namespace magda::nulldiff
