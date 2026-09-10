// peakMagnitude() replaces the scalar `peak = max(peak, fabs(x))` reduction in
// the clipper, compressor and follower push (#2151). That form does not
// vectorise, because reassociating a float reduction needs -ffast-math, which
// the release build does not set. These pin the answer to the loop's.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "magda/daw/core/BlockMath.hpp"

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

TEST_CASE("peakMagnitude walks past a NaN to the real peak", "[dsp][blockmath]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // The peak BEFORE the NaN is the case a SIMD min/max drops: the lane that
    // meets the NaN loses whatever it was carrying.
    CHECK(peakOf({0.9f, nan, 0.1f}) == 0.9f);
    CHECK(peakOf({-0.9f, 0.2f, nan, 0.1f}) == 0.9f);

    // And after it, and on both sides of a vector boundary.
    CHECK(peakOf({nan, 0.75f}) == 0.75f);

    std::vector<float> block(64, 0.05f);
    block[3] = 0.8f;
    block[5] = nan;
    CHECK(peakOf(block) == 0.8f);

    block[40] = nan;
    block[52] = 0.95f;
    CHECK(peakOf(block) == 0.95f);
}

TEST_CASE("peakMagnitude ignores NaN but still counts an infinity", "[dsp][blockmath]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // The loop this replaces ignored NaN, because the compare against it fails,
    // and let an infinity through. Both are load-bearing: a NaN that reached a
    // meter would stick through every later finite block.
    CHECK(peakOf({0.5f, nan}) == 0.5f);
    CHECK(peakOf({nan, nan, nan}) == 0.0f);
    CHECK(peakOf({0.5f, inf}) == inf);
    CHECK(peakOf({0.5f, -inf}) == inf);
    CHECK(peakOf({nan, inf, 0.5f}) == inf);
}

TEST_CASE("peakMagnitude agrees with the loop on blocks holding a NaN", "[dsp][blockmath]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();

    for (int length : {2, 5, 8, 17, 64, 65}) {
        for (int nanAt = 0; nanAt < length; ++nanAt) {
            std::vector<float> samples;
            samples.reserve(static_cast<size_t>(length));
            for (int i = 0; i < length; ++i)
                samples.push_back(std::sin(static_cast<float>(i) * 0.61f));
            samples[static_cast<size_t>(nanAt)] = nan;

            INFO("length " << length << ", NaN at " << nanAt);
            CHECK(peakOf(samples) == peakByLoop(samples));
        }
    }
}

TEST_CASE("peakMagnitude counts a lone spike in the tail", "[dsp][blockmath]") {
    // A peak past the last whole vector is the case a hand-rolled SIMD
    // reduction drops.
    std::vector<float> samples(17, 0.01f);
    samples.back() = -2.5f;

    CHECK(peakOf(samples) == 2.5f);
}

// blockMinMax() is the same reduction with both extremes kept, for the thumbnail
// and media-db column walks (#2152).

namespace {

// The loop blockMinMax() replaces, kept here as the oracle.
std::pair<float, float> minMaxByLoop(const std::vector<float>& samples) {
    float lowest = 1.0f;
    float highest = -1.0f;
    for (float sample : samples) {
        lowest = std::min(lowest, sample);
        highest = std::max(highest, sample);
    }
    return {lowest, highest};
}

juce::Range<float> minMaxOf(const std::vector<float>& samples) {
    return magda::blockMinMax(samples.data(), static_cast<int>(samples.size()));
}

}  // namespace

TEST_CASE("blockMinMax reports both extremes", "[dsp][blockmath]") {
    const auto range = minMaxOf({0.1f, -0.9f, 0.3f});
    CHECK(range.getStart() == -0.9f);
    CHECK(range.getEnd() == 0.3f);
}

TEST_CASE("blockMinMax agrees with the loop it replaces", "[dsp][blockmath]") {
    for (int length : {1, 3, 4, 5, 7, 8, 15, 16, 17, 63, 64, 65, 1023}) {
        std::vector<float> samples;
        samples.reserve(static_cast<size_t>(length));
        for (int i = 0; i < length; ++i)
            samples.push_back(std::sin(static_cast<float>(i) * 0.37f) *
                              (i % 3 == 0 ? -0.9f : 0.6f));

        const auto [lowest, highest] = minMaxByLoop(samples);
        const auto range = minMaxOf(samples);

        INFO("length " << length);
        CHECK(range.getStart() == lowest);
        CHECK(range.getEnd() == highest);
    }
}

TEST_CASE("blockMinMax of an empty or absent block is a zero range", "[dsp][blockmath]") {
    CHECK(magda::blockMinMax(nullptr, 64) == juce::Range<float>());

    const std::vector<float> samples{0.5f};
    CHECK(magda::blockMinMax(samples.data(), 0) == juce::Range<float>());
    CHECK(magda::blockMinMax(samples.data(), -1) == juce::Range<float>());
}

TEST_CASE("blockMinMax walks past a NaN to the real extremes", "[dsp][blockmath]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();

    std::vector<float> block(64, 0.05f);
    block[3] = -0.8f;
    block[5] = nan;
    block[40] = nan;
    block[52] = 0.95f;

    const auto range = minMaxOf(block);
    CHECK(range.getStart() == -0.8f);
    CHECK(range.getEnd() == 0.95f);

    // A block of nothing but NaN has no extremes to report.
    CHECK(minMaxOf({nan, nan, nan}) == juce::Range<float>());
}

TEST_CASE("blockMinMax keeps an all-positive block off zero", "[dsp][blockmath]") {
    // The minimum is a real sample, not a floor the accumulator started at.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(minMaxOf({0.4f, 0.9f}).getStart() == 0.4f);
    CHECK(minMaxOf({0.4f, nan, 0.9f}).getStart() == 0.4f);
}
