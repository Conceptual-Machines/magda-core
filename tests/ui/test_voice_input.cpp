// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "magda/agents/speech_transcriber.hpp"
#include "magda/daw/ui/panels/content/VoiceInput.hpp"

/// @file What the console's mic records, and the rate it hands whisper.

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

TEST_CASE("The mic sums every open input into one channel", "[ui][voice-input]") {
    magda::daw::ui::MonoCapture capture;
    capture.prepare(8);

    std::array<float, 4> left{0.1f, 0.2f, 0.3f, 0.4f};
    std::array<float, 4> right{0.5f, 0.5f, 0.5f, 0.5f};
    const float* inputs[] = {left.data(), nullptr, right.data()};
    capture.append(inputs, 3, 4);

    REQUIRE(capture.size() == 4);
    CHECK(capture.data()[0] == Catch::Approx(0.6f));
    CHECK(capture.data()[3] == Catch::Approx(0.9f));
}

TEST_CASE("The mic stops recording when its buffer is full", "[ui][voice-input]") {
    magda::daw::ui::MonoCapture capture;
    capture.prepare(6);

    std::array<float, 4> block{1.0f, 1.0f, 1.0f, 1.0f};
    const float* inputs[] = {block.data()};
    capture.append(inputs, 1, 4);
    capture.append(inputs, 1, 4);
    capture.append(inputs, 1, 4);

    CHECK(capture.size() == 6);
}

TEST_CASE("Preparing the mic again starts an empty recording", "[ui][voice-input]") {
    magda::daw::ui::MonoCapture capture;
    capture.prepare(4);
    std::array<float, 2> block{1.0f, 1.0f};
    const float* inputs[] = {block.data()};
    capture.append(inputs, 1, 2);

    capture.prepare(4);
    CHECK(capture.size() == 0);
}

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
