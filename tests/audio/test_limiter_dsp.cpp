#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "magda/daw/audio/plugins/compiled/MagdaLimiterCompiledPlugin.hpp"

using magda::daw::audio::compiled::MagdaLimiterDspCore;

namespace {

float peakOf(const juce::AudioBuffer<float>& buffer) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(buffer.getSample(ch, i)));
    return peak;
}

float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

}  // namespace

TEST_CASE("Limiter DSP normalizes threshold into a fixed ceiling", "[limiter][dsp]") {
    juce::AudioBuffer<float> buffer(2, 1024);
    buffer.clear();
    const float input = dbToGain(-12.0f);
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, input);
        buffer.setSample(1, i, -input);
    }

    MagdaLimiterDspCore limiter;
    limiter.prepare(48000.0, buffer.getNumSamples(), buffer.getNumChannels());

    MagdaLimiterDspCore::Settings settings;
    settings.thresholdDb = -12.0f;
    settings.attackMs = 0.1f;
    settings.releaseMs = 10.0f;
    settings.outputDb = 0.0f;

    limiter.process(buffer, 0, buffer.getNumSamples(), settings);

    REQUIRE_THAT(peakOf(buffer), Catch::Matchers::WithinAbs(1.0f, 0.001f));
}

TEST_CASE("Limiter DSP output is post-limiter negative trim", "[limiter][dsp]") {
    juce::AudioBuffer<float> buffer(2, 1024);
    buffer.clear();
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(0, i, 2.0f);
        buffer.setSample(1, i, -2.0f);
    }

    MagdaLimiterDspCore limiter;
    limiter.prepare(48000.0, buffer.getNumSamples(), buffer.getNumChannels());

    MagdaLimiterDspCore::Settings settings;
    settings.thresholdDb = 0.0f;
    settings.attackMs = 0.1f;
    settings.releaseMs = 10.0f;
    settings.outputDb = -6.0f;

    limiter.process(buffer, 0, buffer.getNumSamples(), settings);

    REQUIRE(peakOf(buffer) <= dbToGain(-6.0f) + 0.0001f);
}
