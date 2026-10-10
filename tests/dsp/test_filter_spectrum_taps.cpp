#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "DeviceTestBlock.hpp"
#include "devices/faust/effects/Filter.hpp"

using Catch::Approx;

TEST_CASE("The Filter taps what goes in and what comes out for its faceplate's spectrum",
          "[faust][filter][spectrum]") {
    constexpr int kBlock = 512;
    magda::devices::faust::Filter filter;
    filter.prepare({.sampleRate = 48000.0, .maximumBlockSize = kBlock});

    juce::AudioBuffer<float> buffer(2, kBlock);
    std::fill_n(buffer.getWritePointer(0), kBlock, 0.2f);
    std::fill_n(buffer.getWritePointer(1), kBlock, 0.4f);
    magda::test::DeviceTestBlock block(buffer, kBlock);
    filter.process(block.context);

    std::vector<float> input(kBlock);
    CHECK(filter.getPreSpectrumTapBuffer().readLatest(input.data(), kBlock) == kBlock);
    CHECK(input.front() == Approx(0.3f));  // Both channels, mixed to mono.
    CHECK(input.back() == Approx(0.3f));

    std::vector<float> output(kBlock);
    CHECK(filter.getPostSpectrumTapBuffer().readLatest(output.data(), kBlock) == kBlock);
    const float mixedOut = (buffer.getSample(0, kBlock - 1) + buffer.getSample(1, kBlock - 1)) / 2;
    CHECK(output.back() == Approx(mixedOut));
}

TEST_CASE("The Filter's mix blends a dry signal in phase with the filtered one",
          "[faust][filter][mix]") {
    using Filter = magda::devices::faust::Filter;
    constexpr int kBlock = 512;
    const auto render = [](float mix) {
        Filter filter;
        filter.prepare({.sampleRate = 48000.0, .maximumBlockSize = kBlock});
        filter.setParameterValue(Filter::kCutoffSlot, 0.1f);  // Low, so the filter reshapes it.
        filter.setParameterValue(Filter::kMixSlot, mix);
        juce::AudioBuffer<float> buffer(2, kBlock);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(channel, i, 0.5f * std::sin(i * 0.65f));
        magda::test::DeviceTestBlock block(buffer, kBlock);
        filter.process(block.context);
        return buffer;
    };

    const auto dry = render(0.0f);
    const auto wet = render(1.0f);
    const auto half = render(0.5f);
    float difference = 0.0f;
    for (int i = 0; i < kBlock; ++i) {
        const float input = 0.5f * std::sin(i * 0.65f);
        // Fully dry is the input, sample for sample: no delay between the two paths.
        CHECK(dry.getSample(0, i) == Approx(input).margin(1.0e-6));
        CHECK(half.getSample(0, i) == Approx(0.5f * (input + wet.getSample(0, i))).margin(1.0e-6));
        difference = std::max(difference, std::abs(wet.getSample(0, i) - input));
    }
    CHECK(difference > 0.1f);  // The filter did change the signal.
}
