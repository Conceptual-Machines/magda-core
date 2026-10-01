#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <magda/sdk/dsp/Random.hpp>

TEST_CASE("Lcg48Random reproduces juce::Random for the same seed and calls", "[random]") {
    const std::int64_t seeds[] = {
        0, 1, 42, -1, 0x123456789abcLL, 987654321012345LL, INT64_MIN, INT64_MAX, 1700000000000LL};
    for (const auto seed : seeds) {
        INFO("seed " << seed);
        juce::Random expected(seed);
        magda::sdk::Lcg48Random actual(seed);

        for (int i = 0; i < 200000; ++i) {
            switch (i % 8) {
                case 0:
                    REQUIRE(actual.nextInt() == expected.nextInt());
                    break;
                case 1:
                    REQUIRE(actual.nextInt(16) == expected.nextInt(16));
                    break;
                case 2:
                    REQUIRE(actual.nextInt(7919) == expected.nextInt(7919));
                    break;
                case 3:
                    REQUIRE(actual.nextInt(-5, 12) == expected.nextInt(juce::Range<int>(-5, 7)));
                    break;
                case 4:
                    REQUIRE(actual.nextInt64() == expected.nextInt64());
                    break;
                case 5:
                    REQUIRE(actual.nextBool() == expected.nextBool());
                    break;
                case 6:
                    REQUIRE(actual.nextFloat() == expected.nextFloat());
                    break;
                default:
                    REQUIRE(actual.nextDouble() == expected.nextDouble());
                    break;
            }
        }
    }
}

TEST_CASE("Lcg48Random matches juce::Random on the step-clock call pattern", "[random]") {
    for (const std::int64_t seed : {3, 77, 123456789}) {
        juce::Random expected(seed);
        magda::sdk::Lcg48Random actual(seed);
        for (int i = 0; i < 100000; ++i) {
            const int steps = 1 + (i % 32);
            REQUIRE(actual.nextInt(steps) == expected.nextInt(steps));
        }
    }
}
