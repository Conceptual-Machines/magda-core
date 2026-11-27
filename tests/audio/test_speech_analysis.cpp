// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "magda/daw/audio/analysis/SpeechAnalysis.hpp"

/// @file Phrase, breath and silence regions on synthetic takes with known positions (#3444).

using namespace magda::audio;
using Catch::Approx;

namespace {

constexpr double kRate = 44100.0;
constexpr double kTolerance = 0.05;  // 10 ms frames, 20 ms padding and the onset ramps

/// A take built from parts at known times over a -70 dBFS room.
struct Take {
    std::vector<float> samples;
    std::mt19937 engine{3444};
    std::normal_distribution<double> gaussian;

    explicit Take(double seconds) : samples(static_cast<std::size_t>(seconds * kRate)) {
        for (auto& sample : samples)
            sample = static_cast<float>(gaussian(engine) * 3.16e-4);
    }

    static std::size_t at(double seconds) {
        return static_cast<std::size_t>(seconds * kRate);
    }

    /// 20 ms ramps at both ends, as a voice starts and stops.
    static double ramp(double t, double length) {
        return std::min({1.0, t / 0.02, (length - t) / 0.02});
    }

    /// A voice: 30 harmonics of a 140 Hz pitch with vibrato, at @p rmsDb dBFS.
    void voice(double start, double end, double rmsDb, double f0 = 140.0) {
        const double amplitude = std::pow(10.0, rmsDb / 20.0) / std::sqrt(0.5 * 1.6);
        double phase = 0.0;
        for (auto i = at(start); i < at(end); ++i) {
            const double t = static_cast<double>(i - at(start)) / kRate;
            phase += 2.0 * std::numbers::pi * f0 *
                     (1.0 + 0.01 * std::sin(2.0 * std::numbers::pi * 5.0 * t)) / kRate;
            double value = 0.0;
            for (int h = 1; h <= 30; ++h)
                value += std::sin(h * phase) / h;
            samples[i] += static_cast<float>(amplitude * ramp(t, end - start) * value);
        }
    }

    /// Noise, low-passed by @p smoothing (0 is white), under a Hann envelope.
    void noise(double start, double end, double rmsDb, double smoothing) {
        const double amplitude = std::pow(10.0, rmsDb / 20.0) * 2.0;
        double state = 0.0;
        const double length = end - start;
        for (auto i = at(start); i < at(end); ++i) {
            const double t = static_cast<double>(i - at(start)) / kRate;
            state = smoothing * state + (1.0 - smoothing) * gaussian(engine);
            const double envelope = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * t / length);
            samples[i] += static_cast<float>(amplitude * envelope * state);
        }
    }

    /// Mains hum with harmonics under the whole take, as a fridge or a ground loop adds.
    void hum(double rmsDb) {
        const double amplitude = std::pow(10.0, rmsDb / 20.0);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const double phase = 2.0 * std::numbers::pi * 50.0 * static_cast<double>(i) / kRate;
            samples[i] +=
                static_cast<float>(amplitude * (std::sin(phase) + 0.5 * std::sin(2 * phase) +
                                                0.3 * std::sin(3 * phase)));
        }
    }

    void tone(double start, double end, double peak, double frequency) {
        for (auto i = at(start); i < at(end); ++i) {
            const double t = static_cast<double>(i - at(start)) / kRate;
            samples[i] += static_cast<float>(peak * ramp(t, end - start) *
                                             std::sin(2.0 * std::numbers::pi * frequency * t));
        }
    }

    SpeechAnalysisResult analyse(SpeechAnalysisOptions options = {}, int block = 4096) const {
        SpeechAnalyser analyser(kRate, 1, options);
        for (std::size_t i = 0; i < samples.size(); i += static_cast<std::size_t>(block)) {
            const float* channel = samples.data() + i;
            analyser.process(&channel,
                             static_cast<int>(std::min<std::size_t>(block, samples.size() - i)));
        }
        return analyser.finish();
    }
};

/// Two phrases, a breath between them, then a phrase with a short pause inside it.
Take conversation() {
    Take take(7.0);
    take.voice(0.5, 2.0, -18.0);
    take.noise(2.4, 2.7, -38.0, 0.3);
    take.voice(2.9, 4.4, -26.0);
    take.voice(5.2, 5.5, -20.0);
    take.voice(5.65, 6.0, -20.0);
    return take;
}

}  // namespace

TEST_CASE("Phrases and breaths are found where the take has them", "[speech_analysis]") {
    const auto result = conversation().analyse();

    REQUIRE(result.phrases.size() == 3);
    const double starts[] = {0.5, 2.9, 5.2};
    const double ends[] = {2.0, 4.4, 6.0};
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(result.phrases[i].start == Approx(starts[i]).margin(kTolerance));
        CHECK(result.phrases[i].end == Approx(ends[i]).margin(kTolerance));
    }
    REQUIRE(result.breaths.size() == 1);
    CHECK(result.breaths[0].start == Approx(2.4).margin(kTolerance));
    CHECK(result.breaths[0].end == Approx(2.7).margin(kTolerance));
    CHECK(result.durationSeconds == Approx(7.0).margin(1.0e-3));
}

TEST_CASE("Room hum neither makes phrases of quiet sounds nor hides a breath",
          "[speech_analysis]") {
    auto take = conversation();
    take.hum(-58.0);
    take.noise(4.6, 4.75, -50.0, 0.3);
    const auto result = take.analyse();

    REQUIRE(result.phrases.size() == 3);
    CHECK(result.phrases[0].end == Approx(2.0).margin(kTolerance));
    CHECK(result.phrases[1].start == Approx(2.9).margin(kTolerance));
    REQUIRE(result.breaths.size() == 1);
    CHECK(result.breaths[0].start == Approx(2.4).margin(kTolerance));
}

TEST_CASE("Silences fill every gap between phrases and breaths", "[speech_analysis]") {
    const auto result = conversation().analyse();

    REQUIRE(result.silences.size() == 5);
    CHECK(result.silences.front().start == 0.0);
    CHECK(result.silences.back().end == Approx(7.0).margin(1.0e-3));
    for (const auto& silence : result.silences)
        CHECK(silence.end > silence.start);
    CHECK(result.silences[1].start == Approx(result.phrases[0].end));
    CHECK(result.silences[1].end == Approx(result.breaths[0].start));
}

TEST_CASE("A pause shorter than the shortest silence stays inside its phrase",
          "[speech_analysis]") {
    SpeechAnalysisOptions options;
    options.minSilenceSeconds = 0.1;
    const auto result = conversation().analyse(options);

    REQUIRE(result.phrases.size() == 4);
    CHECK(result.phrases[2].end == Approx(5.5).margin(kTolerance));
    CHECK(result.phrases[3].start == Approx(5.65).margin(kTolerance));
}

TEST_CASE("Phrase loudness follows the voice's level", "[speech_analysis]") {
    const auto result = conversation().analyse();

    REQUIRE(result.phrases.size() == 3);
    CHECK(result.phrases[1].loudness - result.phrases[0].loudness == Approx(-8.0).margin(0.5));
}

TEST_CASE("Loudness is BS.1770: a 997 Hz tone at -20 dBFS peak reads -23 LUFS",
          "[speech_analysis]") {
    Take take(4.0);
    take.tone(1.0, 3.0, 0.1, 997.0);
    const auto result = take.analyse();

    REQUIRE(result.phrases.size() == 1);
    CHECK(result.phrases[0].loudness == Approx(-23.01).margin(0.2));
}

TEST_CASE("Both channels count toward loudness, as BS.1770 sums them", "[speech_analysis]") {
    Take take(4.0);
    take.tone(1.0, 3.0, 0.1, 997.0);
    SpeechAnalyser analyser(kRate, 2);
    const float* channels[] = {take.samples.data(), take.samples.data()};
    analyser.process(channels, static_cast<int>(take.samples.size()));
    const auto result = analyser.finish();

    REQUIRE(result.phrases.size() == 1);
    CHECK(result.phrases[0].loudness == Approx(-20.0).margin(0.2));
}

TEST_CASE("A fricative inside a word neither splits the phrase nor reads as a breath",
          "[speech_analysis]") {
    Take take(4.0);
    take.voice(1.0, 1.6, -20.0);
    take.noise(1.62, 1.75, -30.0, 0.0);
    take.voice(1.77, 2.5, -20.0);
    const auto result = take.analyse();

    REQUIRE(result.phrases.size() == 1);
    CHECK(result.phrases[0].start == Approx(1.0).margin(kTolerance));
    CHECK(result.phrases[0].end == Approx(2.5).margin(kTolerance));
    CHECK(result.breaths.empty());
}

TEST_CASE("A breath longer than the breath range is not a breath", "[speech_analysis]") {
    SpeechAnalysisOptions options;
    options.breathMaxSeconds = 0.2;
    const auto result = conversation().analyse(options);

    CHECK(result.breaths.empty());
    CHECK(result.phrases.size() == 3);
}

TEST_CASE("The block size the audio arrives in does not change the result", "[speech_analysis]") {
    const auto take = conversation();
    const auto whole = take.analyse({}, static_cast<int>(take.samples.size()));
    const auto streamed = take.analyse({}, 333);

    REQUIRE(whole.phrases.size() == streamed.phrases.size());
    for (std::size_t i = 0; i < whole.phrases.size(); ++i) {
        CHECK(whole.phrases[i].start == streamed.phrases[i].start);
        CHECK(whole.phrases[i].end == streamed.phrases[i].end);
        CHECK(whole.phrases[i].loudness == Approx(streamed.phrases[i].loudness).margin(1.0e-6));
    }
    CHECK(whole.breaths.size() == streamed.breaths.size());
}

TEST_CASE("A given threshold replaces the derived one", "[speech_analysis]") {
    SpeechAnalysisOptions options;
    options.thresholdDb = -23.0;
    const auto result = conversation().analyse(options);

    CHECK(result.thresholdDb == -23.0);
    // The -26 dBFS phrase and the breath sit under -23 dBFS.
    REQUIRE(result.phrases.size() == 2);
    CHECK(result.phrases[0].start == Approx(0.5).margin(kTolerance));
    CHECK(result.phrases[1].start == Approx(5.2).margin(kTolerance));
    CHECK(result.breaths.empty());
}

TEST_CASE("A take with digital silence and no room finds its phrase", "[speech_analysis]") {
    Take take(3.0);
    std::ranges::fill(take.samples, 0.0f);
    take.voice(1.0, 2.0, -20.0, 220.0);
    const auto result = take.analyse();

    REQUIRE(result.phrases.size() == 1);
    CHECK(result.phrases[0].start == Approx(1.0).margin(kTolerance));
    CHECK(result.phrases[0].end == Approx(2.0).margin(kTolerance));
}

TEST_CASE("Digital silence is one silence and no phrases", "[speech_analysis]") {
    SpeechAnalyser analyser(kRate, 1);
    std::vector<float> zeros(static_cast<std::size_t>(kRate * 2.0), 0.0f);
    const float* channel = zeros.data();
    analyser.process(&channel, static_cast<int>(zeros.size()));
    const auto result = analyser.finish();

    CHECK(result.phrases.empty());
    CHECK(result.breaths.empty());
    REQUIRE(result.silences.size() == 1);
    CHECK(result.silences[0].end == Approx(2.0).margin(1.0e-3));
}
