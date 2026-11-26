// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/agents/speech_transcriber.hpp"

/// @file The rate and level of the audio SpeechTranscriber hands whisper.

namespace {

std::vector<float> tone(double hz, double sampleRate, int numSamples) {
    std::vector<float> samples(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        samples[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / sampleRate));
    return samples;
}

/// Peak over the back half, past the filters' settling.
float settledPeak(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (auto i = samples.size() / 2; i < samples.size(); ++i)
        peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

}  // namespace

TEST_CASE("Speech reaches whisper at 16 kHz with its level kept", "[agents][speech]") {
    constexpr double kRate = 48000.0;
    const auto input = tone(440.0, kRate, 48000);
    const auto output = magda::SpeechTranscriber::toWhisperRate(
        input.data(), static_cast<int>(input.size()), kRate);

    CHECK(output.size() == 16000);
    CHECK(settledPeak(output) == Catch::Approx(1.0f).margin(0.05f));
}

TEST_CASE("What 16 kHz cannot carry is filtered out, not folded back", "[agents][speech]") {
    constexpr double kRate = 48000.0;
    const auto input = tone(14000.0, kRate, 48000);
    const auto output = magda::SpeechTranscriber::toWhisperRate(
        input.data(), static_cast<int>(input.size()), kRate);

    CHECK(settledPeak(output) < 0.1f);
}

TEST_CASE("Audio already at 16 kHz passes through", "[agents][speech]") {
    const auto input = tone(440.0, 16000.0, 1600);
    const auto output = magda::SpeechTranscriber::toWhisperRate(
        input.data(), static_cast<int>(input.size()), 16000.0);

    REQUIRE(output.size() == input.size());
    CHECK(settledPeak(output) == Catch::Approx(1.0f).margin(0.01f));
}

TEST_CASE("No audio resamples to none", "[agents][speech]") {
    CHECK(magda::SpeechTranscriber::toWhisperRate(nullptr, 0, 48000.0).empty());
}
