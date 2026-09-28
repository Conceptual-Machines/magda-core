#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "magda/daw/ui/components/mixer/HardwareInputLevels.hpp"

/// @file The input levels behind the input menu's meters.

namespace {

juce::BigInteger channels(std::initializer_list<int> set) {
    juce::BigInteger result;
    for (const auto channel : set)
        result.setBit(channel);
    return result;
}

}  // namespace

TEST_CASE("Input levels land on the physical channel, not the callback index",
          "[ui][input-levels]") {
    magda::InputChannelLevels levels;
    levels.setActiveInputs(channels({4, 5}), 48000.0);

    std::array<float, 480> left{}, right{};
    left[10] = 0.5f;
    right[20] = -0.8f;
    const float* inputs[] = {left.data(), right.data()};
    levels.measure(inputs, 2, 480);

    CHECK(levels.level(4) == Catch::Approx(0.5f));
    CHECK(levels.level(5) == Catch::Approx(0.8f));
    CHECK(levels.level(0) == 0.0f);
    CHECK(levels.level(-1) == 0.0f);
    CHECK(levels.level(magda::InputChannelLevels::kMaxChannels) == 0.0f);
}

TEST_CASE("A peak decays rather than vanishing at the next block", "[ui][input-levels]") {
    magda::InputChannelLevels levels;
    levels.setActiveInputs(channels({0}), 48000.0);

    std::array<float, 480> block{};
    block[0] = 1.0f;
    const float* inputs[] = {block.data()};
    levels.measure(inputs, 1, 480);

    block[0] = 0.0f;
    levels.measure(inputs, 1, 480);

    // 480 samples at 48 kHz is a tenth of the 0.1 s release.
    CHECK(levels.level(0) == Catch::Approx(std::exp(-0.1f)));
}

TEST_CASE("A device restart starts the levels again", "[ui][input-levels]") {
    magda::InputChannelLevels levels;
    levels.setActiveInputs(channels({0}), 48000.0);

    std::array<float, 64> block{};
    block.fill(0.25f);
    const float* inputs[] = {block.data()};
    levels.measure(inputs, 1, 64);
    REQUIRE(levels.level(0) > 0.0f);

    levels.setActiveInputs({}, 0.0);
    CHECK(levels.level(0) == 0.0f);

    // Nothing is open, so nothing is attributed to a channel.
    levels.measure(inputs, 1, 64);
    CHECK(levels.level(0) == 0.0f);
}
