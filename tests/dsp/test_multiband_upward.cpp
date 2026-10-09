#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

#include "DeviceTestBlock.hpp"
#include "devices/faust/effects/Multiband.hpp"

namespace {

using Multiband = magda::devices::faust::Multiband;
constexpr int kBlock = 512;
constexpr double kRate = 48000.0;

double power(const juce::AudioBuffer<float>& buffer, int from, int to) {
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += buffer.getSample(0, i) * buffer.getSample(0, i);
    return sum / (to - from);
}

void setOttCurve(Multiband& multiband) {
    auto threshold = [](float db) { return (db + 80.0f) / 80.0f; };
    auto gain = [](float db) { return (db + 24.0f) / 48.0f; };
    const int upper[] = {Multiband::kLowUpperThresholdSlot, Multiband::kMidUpperThresholdSlot,
                         Multiband::kHighUpperThresholdSlot};
    const int lower[] = {Multiband::kLowLowerThresholdSlot, Multiband::kMidLowerThresholdSlot,
                         Multiband::kHighLowerThresholdSlot};
    const int above[] = {Multiband::kLowAboveRatioSlot, Multiband::kMidAboveRatioSlot,
                         Multiband::kHighAboveRatioSlot};
    const int below[] = {Multiband::kLowBelowRatioSlot, Multiband::kMidBelowRatioSlot,
                         Multiband::kHighBelowRatioSlot};
    const int range[] = {Multiband::kLowRangeSlot, Multiband::kMidRangeSlot,
                         Multiband::kHighRangeSlot};
    const int output[] = {Multiband::kLowGainSlot, Multiband::kMidGainSlot,
                          Multiband::kHighGainSlot};
    // Ableton's OTT preset, the curve Xfer OTT is built on.
    const float upperDb[] = {-33.8f, -30.2f, -35.5f};
    const float lowerDb[] = {-40.8f, -41.8f, -40.8f};
    const float outputDb[] = {10.3f, 5.7f, 10.3f};
    multiband.setParameterValue(Multiband::kAmountSlot, 1.0f);
    for (int band = 0; band < 3; ++band) {
        multiband.setParameterValue(upper[band], threshold(upperDb[band]));
        multiband.setParameterValue(lower[band], threshold(lowerDb[band]));
        multiband.setParameterValue(above[band], 1.0f);
        multiband.setParameterValue(below[band], (4.17f - 0.05f) / 99.95f);
        multiband.setParameterValue(range[band], 1.0f);
        multiband.setParameterValue(output[band], gain(outputDb[band]));
    }
}

}  // namespace

TEST_CASE("Multiband lifts the noise floor between hits like OTT", "[multiband][dsp]") {
    // Loud 100 ms noise bursts every 500 ms over a -60 dBFS noise floor, 54 dB under the bursts.
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const int total = static_cast<int>(kRate * 4);
    const int period = static_cast<int>(kRate * 0.5);
    const int burst = static_cast<int>(kRate * 0.1);
    juce::AudioBuffer<float> audio(2, total);
    for (int i = 0; i < total; ++i)
        audio.setSample(0, i, (i % period) < burst ? 0.5f * noise(random) : 0.001f * noise(random));
    audio.copyFrom(1, 0, audio, 0, 0, total);

    Multiband multiband;
    multiband.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
    setOttCurve(multiband);
    for (int start = 0; start < total; start += kBlock) {
        juce::AudioBuffer<float> chunk(audio.getArrayOfWritePointers(), 2, start, kBlock);
        magda::test::DeviceTestBlock block(chunk, kBlock);
        multiband.process(block.context);
    }

    double hits = 0.0, gaps = 0.0;
    for (int start = total / 2; start + period <= total; start += period) {
        hits += power(audio, start + burst / 2, start + burst);
        gaps += power(audio, start + period - burst * 2, start + period);
    }
    // Xfer OTT at its defaults leaves the floor 15 dB under the hits; a linear-domain release
    // left it 36 dB under.
    CHECK(10.0 * std::log10(gaps / hits) > -20.0);
}
