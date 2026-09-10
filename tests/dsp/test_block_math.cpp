// peakMagnitude() replaces the scalar `peak = max(peak, fabs(x))` reduction in
// the clipper, compressor and follower push (#2151). That form does not
// vectorise, because reassociating a float reduction needs -ffast-math, which
// the release build does not set. These pin the answer to the loop's.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "magda/daw/audio/BlockMath.hpp"

using magda::peakMagnitude;

namespace {

// The loop peakMagnitude() replaces, kept here as the oracle.
float peakByLoop(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float sample : samples)
        peak = std::max(peak, std::fabs(sample));
    return peak;
}

float peakOf(const std::vector<float>& samples) {
    return peakMagnitude(samples.data(), static_cast<int>(samples.size()));
}

}  // namespace

TEST_CASE("peakMagnitude finds the loudest sample either side of zero", "[dsp][blockmath]") {
    CHECK(peakOf({0.1f, -0.9f, 0.3f}) == 0.9f);
    CHECK(peakOf({0.1f, 0.9f, -0.3f}) == 0.9f);
}

TEST_CASE("peakMagnitude agrees with the loop it replaces", "[dsp][blockmath]") {
    // Lengths either side of a SIMD width, so the tail is covered too.
    for (int length : {1, 3, 4, 5, 7, 8, 15, 16, 17, 63, 64, 65, 1023}) {
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(length));
        for (int i = 0; i < length; ++i)
            samples.push_back(std::sin(static_cast<float>(i) * 0.37f) *
                              (i % 3 == 0 ? -1.4f : 0.6f));

        INFO("length " << length);
        CHECK(peakOf(samples) == peakByLoop(samples));
    }
}

TEST_CASE("peakMagnitude of silence is zero, not a floor", "[dsp][blockmath]") {
    CHECK(peakOf({0.0f, 0.0f, 0.0f, 0.0f}) == 0.0f);
    CHECK(peakOf({-0.0f, 0.0f}) == 0.0f);
}

TEST_CASE("peakMagnitude of an empty or absent block is zero", "[dsp][blockmath]") {
    CHECK(peakMagnitude(nullptr, 64) == 0.0f);

    const std::vector<float> samples{0.5f};
    CHECK(peakMagnitude(samples.data(), 0) == 0.0f);
    CHECK(peakMagnitude(samples.data(), -1) == 0.0f);
}

TEST_CASE("peakMagnitude counts a lone spike in the tail", "[dsp][blockmath]") {
    // A peak past the last whole vector is the case a hand-rolled SIMD
    // reduction drops.
    std::vector<float> samples(17, 0.01f);
    samples.back() = -2.5f;

    CHECK(peakOf(samples) == 2.5f);
}
