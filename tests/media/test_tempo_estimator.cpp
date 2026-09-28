// The measured BPM tier (#2674).
//
// Two kinds of case. The synthetic ones pin the algorithm against signals whose
// tempo is known by construction. The corpus one is hidden ([.]) and measures
// it against a real sample library, which is the only thing that says whether
// it works -- see the tag's own test for what it reports.

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <vector>

#include "../../magda/daw/media_db/TempoEstimator.hpp"

using Catch::Approx;
using magda::media::estimateTempo;

namespace {

constexpr double kHopSeconds = 256.0 / 44100.0;  // the indexer's hop

/// An onset envelope with a spike every beat, as a steady loop produces.
std::vector<float> clickEnvelope(double bpm, double seconds, float jitter = 0.0F) {
    const auto frames = static_cast<std::size_t>(seconds / kHopSeconds);
    std::vector<float> envelope(frames, 0.05F);
    const double framesPerBeat = 60.0 / bpm / kHopSeconds;
    juce::Random random(1234);
    for (double position = 0.0; position < static_cast<double>(frames); position += framesPerBeat) {
        const auto frame = static_cast<std::size_t>(std::llround(position));
        if (frame < frames) {
            // A flux peak is a few frames wide, not one: an onset is never
            // aligned to the analysis hop.
            envelope[frame] = 1.0F + jitter * random.nextFloat();
            for (std::size_t tail = 1; tail < 4 && frame + tail < frames; ++tail) {
                envelope[frame + tail] = envelope[frame] * (0.6F / static_cast<float>(tail));
            }
        }
    }
    return envelope;
}

}  // namespace

TEST_CASE("A steady pulse is measured at its own tempo", "[media][tempo]") {
    // No duration passed, so the whole-bar refinement stays out of it: what is
    // measured here is the envelope. Eight seconds is not a whole number of
    // beats at most of these tempi anyway.
    for (double bpm : {90.0, 120.0, 128.0, 140.0, 174.0}) {
        const auto measured = estimateTempo(clickEnvelope(bpm, 8.0), kHopSeconds, 0.0);
        REQUIRE(measured.has_value());
        REQUIRE(measured->bpm == Approx(bpm).margin(1.0));
        REQUIRE(measured->confidence > 0.35);
    }
}

TEST_CASE("A loop's length pins the tempo exactly", "[media][tempo]") {
    // 16 beats at 174 is 5.51724s, which is the case that started #2674. The
    // envelope alone lands near it; the whole-bar snap lands on it.
    constexpr double duration = 16.0 * 60.0 / 174.0;
    const auto measured = estimateTempo(clickEnvelope(174.0, duration), kHopSeconds, duration);

    REQUIRE(measured.has_value());
    REQUIRE(measured->wholeBars);
    REQUIRE(measured->bpm == Approx(174.0).margin(0.05));
}

TEST_CASE("An accent pattern keeps the tempo it defines", "[media][tempo]") {
    // Onsets on every eighth, accents on the quarters. A uniform pulse at the
    // eighth rate would be read at the eighth rate -- what makes this 120 and
    // not 240 is that the accented period correlates better, which is the
    // check the octave step has to respect rather than always taking the
    // faster reading.
    constexpr double quarterBpm = 120.0;
    auto envelope = clickEnvelope(quarterBpm * 2.0, 12.0);
    const double framesPerEighth = 60.0 / (quarterBpm * 2.0) / kHopSeconds;
    for (int eighth = 0; eighth * framesPerEighth < static_cast<double>(envelope.size());
         eighth += 2) {
        const auto frame = static_cast<std::size_t>(std::llround(eighth * framesPerEighth));
        if (frame < envelope.size()) {
            envelope[frame] = 3.0F;
        }
    }

    const auto measured = estimateTempo(envelope, kHopSeconds, 12.0);
    REQUIRE(measured.has_value());
    REQUIRE(measured->bpm == Approx(quarterBpm).margin(2.0));
}

TEST_CASE("Material with no tempo in it says so", "[media][tempo]") {
    SECTION("Silence") {
        const std::vector<float> envelope(2000, 0.0F);
        const auto measured = estimateTempo(envelope, kHopSeconds, 12.0);
        REQUIRE((!measured.has_value() || measured->confidence < 0.35));
    }

    SECTION("A pad: a slow swell and nothing periodic") {
        std::vector<float> envelope(2000, 0.0F);
        for (std::size_t i = 0; i < envelope.size(); ++i) {
            envelope[i] = static_cast<float>(
                0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / 1800.0));
        }
        const auto measured = estimateTempo(envelope, kHopSeconds, 12.0);
        REQUIRE((!measured.has_value() || measured->confidence < 0.35));
    }

    SECTION("A one-shot: one transient and decay") {
        std::vector<float> envelope(400, 0.01F);
        envelope[10] = 1.0F;
        const auto measured = estimateTempo(envelope, kHopSeconds, 2.3);
        REQUIRE((!measured.has_value() || measured->confidence < 0.35));
    }
}

TEST_CASE("Too little to go on is not an answer", "[media][tempo]") {
    REQUIRE(!estimateTempo({}, kHopSeconds, 4.0).has_value());
    REQUIRE(!estimateTempo(clickEnvelope(120.0, 0.5), kHopSeconds, 0.5).has_value());
}
