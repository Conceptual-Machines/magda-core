#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
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

TEST_CASE("Multiband thresholds may cross, both stages acting between them", "[multiband][dsp]") {
    // A 1 kHz sine peaking at -30 dBFS sits in the mid band between -20 and -40 dB thresholds.
    auto midGainDb = [](float lowerDb, float upperDb) {
        const int total = static_cast<int>(kRate);
        juce::AudioBuffer<float> audio(2, total);
        const float peak = std::pow(10.0f, -30.0f / 20.0f);
        for (int i = 0; i < total; ++i)
            audio.setSample(0, i,
                            peak * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f *
                                            static_cast<float>(i) / static_cast<float>(kRate)));
        audio.copyFrom(1, 0, audio, 0, 0, total);
        const double inputPower = power(audio, total / 2, total);

        Multiband multiband;
        multiband.prepare({.sampleRate = kRate, .maximumBlockSize = kBlock});
        multiband.setParameterValue(Multiband::kAmountSlot, 1.0f);
        // The sine's skirts in the low and high bands would be lifted by their own stages.
        multiband.setParameterValue(Multiband::kLowRangeSlot, 0.0f);
        multiband.setParameterValue(Multiband::kHighRangeSlot, 0.0f);
        multiband.setParameterValue(Multiband::kMidLowerThresholdSlot, (lowerDb + 80.0f) / 80.0f);
        multiband.setParameterValue(Multiband::kMidUpperThresholdSlot, (upperDb + 80.0f) / 80.0f);
        multiband.setParameterValue(Multiband::kMidBelowRatioSlot, (4.0f - 0.05f) / 99.95f);
        multiband.setParameterValue(Multiband::kMidAboveRatioSlot, (2.0f - 0.05f) / 99.95f);
        for (int start = 0; start < total; start += kBlock) {
            const int samples = std::min(kBlock, total - start);
            juce::AudioBuffer<float> chunk(audio.getArrayOfWritePointers(), 2, start, samples);
            magda::test::DeviceTestBlock block(chunk, samples);
            multiband.process(block.context);
        }
        return 10.0 * std::log10(power(audio, total / 2, total) / inputPower);
    };

    // Uncrossed, the level is between the stages and untouched.
    CHECK(std::abs(midGainDb(-40.0f, -20.0f)) < 0.5);
    // Crossed, both act: -35 - 1.25 L dB, +2.5 at the peak and +3.6 at the detector's reading,
    // about 1 dB under it.
    const double crossed = midGainDb(-20.0f, -40.0f);
    CHECK(crossed > 2.5);
    CHECK(crossed < 4.5);
}
