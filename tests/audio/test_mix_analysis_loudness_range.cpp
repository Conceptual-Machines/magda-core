// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "../../magda/daw/audio/analysis/MixAnalysisInput.hpp"

using magda::daw::audio::MixAnalysisInput;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

/** @brief 20 s of a stereo 1 kHz sine at each level, the first EBU Tech 3342 test signal. */
juce::AudioBuffer<float> twoLevelTone(double firstDb, double secondDb) {
    const int half = static_cast<int>(20.0 * kSr);
    juce::AudioBuffer<float> buf(2, 2 * half);
    for (int i = 0; i < buf.getNumSamples(); ++i) {
        const double db = i < half ? firstDb : secondDb;
        const auto s =
            static_cast<float>(std::pow(10.0, db / 20.0) * std::sin(2.0 * kPi * 1000.0 * i / kSr));
        buf.setSample(0, i, s);
        buf.setSample(1, i, s);
    }
    return buf;
}

}  // namespace

TEST_CASE("Mix analysis carries each track's loudness range", "[mix_analysis][lra]") {
    const auto track = MixAnalysisInput::fingerprint(twoLevelTone(-20.0, -30.0), kSr, "Tone", "");
    REQUIRE(track.loudnessRangeValid);
    CHECK(track.loudnessRange == Catch::Approx(10.0f).margin(1.0f));
}

TEST_CASE("Mix analysis has no loudness range for a clip shorter than 3 s", "[mix_analysis][lra]") {
    juce::AudioBuffer<float> shortClip(2, static_cast<int>(2.0 * kSr));
    shortClip.clear();
    for (int i = 0; i < shortClip.getNumSamples(); ++i)
        shortClip.setSample(0, i,
                            0.1f * static_cast<float>(std::sin(2.0 * kPi * 1000.0 * i / kSr)));
    const auto track = MixAnalysisInput::fingerprint(shortClip, kSr, "Short", "");
    CHECK_FALSE(track.loudnessRangeValid);
}
