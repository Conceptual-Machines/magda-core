#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
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
