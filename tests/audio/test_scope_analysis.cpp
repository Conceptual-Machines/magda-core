// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

#include "../../magda/daw/audio/analysis/ScopeAnalysis.hpp"

using magda::daw::audio::envelopeColumns;
using magda::daw::audio::findRisingTrigger;
using magda::daw::audio::kScopeSincHalfWidth;
using magda::daw::audio::scopeSpan;
using magda::daw::audio::sincSample;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxWindow = 262144;

std::vector<float> sine(double hz, int numSamples, double phase = 0.0) {
    std::vector<float> out(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2.0 * kPi * hz * i / kSr + phase));
    return out;
}

/// Where in its period the trigger found in samples [start, start + span) lands.
double triggerPhase(const std::vector<float>& signal, int start, double period) {
    const auto span = scopeSpan(kSr, 10.0, kMaxWindow);
    const double t = findRisingTrigger(signal.data() + start, span.readCount, span.searchSamples);
    REQUIRE(t >= 0.0);
    return std::fmod(start + t, period);
}

}  // namespace

TEST_CASE("The read covers a 20 Hz period and fits the tap ring", "[scope][3186]") {
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (double ms : {1.0, 10.0, 1000.0, 5000.0}) {
            const auto span = scopeSpan(sr, ms, kMaxWindow);
            CHECK(span.searchSamples >= sr / 20.0);
            CHECK(span.readCount <= kMaxWindow);
            CHECK(span.readCount == span.searchSamples + span.displaySamples + kScopeSincHalfWidth);
        }
}

TEST_CASE("A sawtooth with added noise triggers at the same phase every read", "[scope][3186]") {
    constexpr double period = 480.0;  // 100 Hz
    std::mt19937 rng(3186);
    std::uniform_real_distribution<float> noise(-0.02f, 0.02f);
    std::vector<float> saw(48000);
    for (size_t i = 0; i < saw.size(); ++i) {
        const double frac = std::fmod(static_cast<double>(i) / period, 1.0);
        saw[i] = static_cast<float>(0.8 * (2.0 * frac - 1.0)) + noise(rng);
    }

    // The ramp crosses zero halfway through each period. The first raw zero
    // crossing strays up to 6 samples on this noise.
    std::uniform_int_distribution<int> start(0, 40000);
    for (int read = 0; read < 200; ++read)
        CHECK(triggerPhase(saw, start(rng), period) == Catch::Approx(240.0).margin(1.0));
}

TEST_CASE("A drum mix scrolls with the audio instead of holding then jumping", "[scope][3186]") {
    // A kick every half second with hats, over two tones.
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> hats(-0.5f, 0.5f);
    std::vector<float> mix(48000 * 6);
    for (size_t i = 0; i < mix.size(); ++i) {
        const double k = static_cast<double>(i % 24000);
        const double kick = 0.9 * std::exp(-k / 2400.0) *
                            std::sin(2.0 * kPi * (60.0 + 200.0 * std::exp(-k / 400.0)) * k / kSr);
        const double tones =
            std::sin(2.0 * kPi * 110.0 * i / kSr) + std::sin(2.0 * kPi * 165.5 * i / kSr);
        mix[i] = static_cast<float>(0.63 * kick + 0.15 * tones) +
                 0.2f * static_cast<float>(std::exp(-static_cast<double>(i % 12000) / 300.0)) *
                     hats(rng);
    }

    // Streamed in 512-sample blocks, the drawn span moves about a block a frame.
    // The latest-trigger version leapt 6363 samples here; the first crossing, 1064.
    for (double ms : {10.0, 1000.0}) {
        const auto span = scopeSpan(kSr, ms, kMaxWindow);
        double previous = -1.0;
        for (int end = span.readCount; end <= static_cast<int>(mix.size()); end += 512) {
            const int start = end - span.readCount;
            double t = findRisingTrigger(mix.data() + start, span.readCount, span.searchSamples);
            if (t < 0.0)
                t = 0.5 * span.searchSamples;
            if (previous >= 0.0)
                CHECK(std::abs(start + t - previous) < 2000.0);
            previous = start + t;
        }
    }
}

TEST_CASE("A 20 Hz sine locks on its rising zero crossing", "[scope][3186]") {
    constexpr double period = kSr / 20.0;
    const auto signal = sine(20.0, 96000);
    for (int start : {0, 517, 1200, 2399, 30011})
        CHECK(std::abs(std::remainder(triggerPhase(signal, start, period), period)) < 0.5);
}

TEST_CASE("The trigger falls between samples", "[scope][3186]") {
    const auto signal = sine(1000.0, 4096, -0.3);  // crosses zero 2.29 samples in
    const auto span = scopeSpan(kSr, 10.0, kMaxWindow);
    const double t = findRisingTrigger(signal.data(), span.readCount, span.searchSamples);
    CHECK(std::fmod(t, 48.0) == Catch::Approx(0.3 * kSr / (2.0 * kPi * 1000.0)).margin(0.02));
}

TEST_CASE("Silence does not trigger", "[scope][3186]") {
    const std::vector<float> silence(4096, 0.0f);
    CHECK(findRisingTrigger(silence.data(), 4096, 2048) < 0.0);
}

TEST_CASE("A sine of a few samples per cycle interpolates as a smooth curve", "[scope][3186]") {
    for (double hz : {4000.0, 8000.0, 12000.0}) {
        const auto signal = sine(hz, 512);
        float worst = 0.0f;
        for (double t = 100.0; t < 400.0; t += 0.125) {
            const auto exact = static_cast<float>(std::sin(2.0 * kPi * hz * t / kSr));
            worst = std::max(worst, std::abs(sincSample(signal.data(), 512, t) - exact));
        }
        CHECK(worst < 0.02f);
    }
}

TEST_CASE("A dense mix's envelope moves no further than the trigger does", "[scope][3186]") {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-0.3f, 0.3f);
    std::vector<float> mix(40000);
    for (size_t i = 0; i < mix.size(); ++i)
        mix[i] = 0.3f * static_cast<float>(std::sin(2.0 * kPi * 55.0 * i / kSr) +
                                           std::sin(2.0 * kPi * 1234.5 * i / kSr)) +
                 noise(rng);

    std::vector<float> mins, maxs, nudgedMins, nudgedMaxs;
    envelopeColumns(mix.data(), 40000, 1000.0, 137.3, 200, mins, maxs);
    envelopeColumns(mix.data(), 40000, 1000.01, 137.3, 200, nudgedMins, nudgedMaxs);

    // A 0.01-sample nudge moves an edge by 0.01 of a step, a step being at most 1.8 here.
    for (size_t c = 0; c < mins.size(); ++c) {
        CHECK(std::abs(nudgedMins[c] - mins[c]) <= 0.02f);
        CHECK(std::abs(nudgedMaxs[c] - maxs[c]) <= 0.02f);
    }
}
