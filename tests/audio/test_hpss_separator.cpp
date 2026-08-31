// HPSS stem separation (issue #1288): model-level tests for the pure-DSP
// harmonic/percussive separator. A steady sine must land almost entirely in
// the harmonic stem, a click train in the percussive stem, and the two stems
// must always sum back to the input (the soft masks are complementary).

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>

#include "stem_separation/HpssSeparator.hpp"

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kNumSamples = 1 << 16;  // ~1.5 s

float rms(const juce::AudioBuffer<float>& buffer, int channel) {
    double sum = 0.0;
    const float* data = buffer.getReadPointer(channel);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return static_cast<float>(std::sqrt(sum / buffer.getNumSamples()));
}

juce::AudioBuffer<float> makeSine(double freqHz, int channels = 1) {
    juce::AudioBuffer<float> buffer(channels, kNumSamples);
    for (int ch = 0; ch < channels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < kNumSamples; ++i)
            data[i] = 0.5F * static_cast<float>(
                                 std::sin(2.0 * std::numbers::pi * freqHz * i / kSampleRate));
    }
    return buffer;
}

// A click every `periodSamples`: broadband, zero sustain. The period must
// exceed the separator's time-median kernel span (17 frames x 1024 hop),
// otherwise clicks fill the median window and read as sustained energy.
juce::AudioBuffer<float> makeClicks(int periodSamples) {
    juce::AudioBuffer<float> buffer(1, kNumSamples);
    buffer.clear();
    float* data = buffer.getWritePointer(0);
    // Start half a period in: a click at the very first/last frames sits at
    // the clamped edge of the time-median window, where nearest-frame
    // replication makes it read as sustained.
    for (int i = periodSamples / 2; i < kNumSamples; i += periodSamples)
        data[i] = 0.9F;
    return buffer;
}

}  // namespace

TEST_CASE("HPSS reports two stems", "[stems][hpss]") {
    magda::stems::HpssSeparator hpss;
    const auto names = hpss.stemNames();
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "Harmonic");
    CHECK(names[1] == "Percussive");
    CHECK(juce::String(hpss.modelId()) == "hpss");
}

TEST_CASE("Steady sine separates into the harmonic stem", "[stems][hpss]") {
    magda::stems::HpssSeparator hpss;
    const auto input = makeSine(440.0);

    auto stems = hpss.separate(input, kSampleRate, nullptr);
    REQUIRE(stems.size() == 2);
    REQUIRE(stems[0].audio.getNumSamples() == kNumSamples);

    const float harmRms = rms(stems[0].audio, 0);
    const float percRms = rms(stems[1].audio, 0);
    CHECK(harmRms > 10.0F * percRms);
}

TEST_CASE("Click train separates into the percussive stem", "[stems][hpss]") {
    magda::stems::HpssSeparator hpss;
    const auto input = makeClicks(20000);

    auto stems = hpss.separate(input, kSampleRate, nullptr);
    REQUIRE(stems.size() == 2);

    const float harmRms = rms(stems[0].audio, 0);
    const float percRms = rms(stems[1].audio, 0);
    CHECK(percRms > 10.0F * harmRms);
}

TEST_CASE("Stems sum back to the input", "[stems][hpss]") {
    magda::stems::HpssSeparator hpss;
    auto input = makeSine(440.0, 2);
    // Add clicks on top so both stems carry energy.
    for (int ch = 0; ch < 2; ++ch) {
        float* data = input.getWritePointer(ch);
        for (int i = 0; i < kNumSamples; i += 8192)
            data[i] += 0.4F;
    }

    auto stems = hpss.separate(input, kSampleRate, nullptr);
    REQUIRE(stems.size() == 2);
    REQUIRE(stems[0].audio.getNumChannels() == 2);

    for (int ch = 0; ch < 2; ++ch) {
        const float* src = input.getReadPointer(ch);
        const float* h = stems[0].audio.getReadPointer(ch);
        const float* p = stems[1].audio.getReadPointer(ch);
        float maxErr = 0.0F;
        for (int i = 0; i < kNumSamples; ++i)
            maxErr = std::max(maxErr, std::abs(src[i] - (h[i] + p[i])));
        CHECK(maxErr < 1.0e-3F);
    }
}

TEST_CASE("Cancellation via progress returns empty", "[stems][hpss]") {
    magda::stems::HpssSeparator hpss;
    const auto input = makeSine(440.0);

    auto stems = hpss.separate(input, kSampleRate, [](float) { return false; });
    CHECK(stems.empty());
}

TEST_CASE("Progress is monotonic and reaches 1", "[stems][hpss]") {
    magda::stems::HpssSeparator hpss;
    const auto input = makeSine(440.0);

    float last = -1.0F;
    bool monotonic = true;
    auto stems = hpss.separate(input, kSampleRate, [&](float frac) {
        monotonic = monotonic && frac >= last;
        last = frac;
        return true;
    });
    REQUIRE_FALSE(stems.empty());
    CHECK(monotonic);
    CHECK(last == 1.0F);
}
