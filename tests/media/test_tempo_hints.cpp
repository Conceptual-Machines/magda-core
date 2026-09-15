// The arbiter between what the audio measured and what a file claims (#2674).
//
// Pure functions over a measurement and a pair of claims: no decoding, no FFT.
// What the claims are allowed to do is pick an octave and nothing else.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "../../magda/daw/media_db/TempoEstimator.hpp"

using Catch::Approx;
using magda::media::hintAgrees;
using magda::media::kMinTempoConfidence;
using magda::media::refineTempo;
using magda::media::resolveTempo;
using magda::media::TempoEstimate;
using magda::media::TempoHints;

namespace {

TempoHints named(double bpm) {
    return {bpm, std::nullopt};
}

TempoEstimate measured(double bpm, double confidence) {
    TempoEstimate estimate;
    estimate.bpm = bpm;
    estimate.confidence = confidence;
    return estimate;
}

}  // namespace

TEST_CASE("A hint picks an octave of the measured tempo", "[media][tempo]") {
    REQUIRE(refineTempo(87.0, 0.0, named(174.0)) == Approx(174.0));
    REQUIRE(refineTempo(174.0, 0.0, named(87.0)) == Approx(87.0));
    // 120 is no octave of 128, so the measurement stands and the claim is lost.
    REQUIRE(refineTempo(128.0, 0.0, named(120.0)) == Approx(128.0));
}

TEST_CASE("A hint outside the search range picks nothing", "[media][tempo]") {
    // Half of 70 and double 110 are both outside [kMinTempoBpm, kMaxTempoBpm],
    // so neither is a candidate for a hint to land on.
    REQUIRE(refineTempo(70.0, 0.0, named(35.0)) == Approx(70.0));
    REQUIRE(refineTempo(110.0, 0.0, named(220.0)) == Approx(110.0));
}

TEST_CASE("The name is read before the metadata chunk", "[media][tempo]") {
    REQUIRE(refineTempo(87.0, 0.0, {std::nullopt, 174.0}) == Approx(174.0));
    // The name agrees with the measurement, so the chunk's octave never gets a say.
    REQUIRE(refineTempo(87.0, 0.0, {87.5, 174.0}) == Approx(87.0));
}

TEST_CASE("A loop's length snaps the measurement to whole bars", "[media][tempo]") {
    constexpr double kDurationS = 16.0 * 60.0 / 174.0;  // 5.5172 s
    REQUIRE(refineTempo(173.4, kDurationS, {}) == Approx(174.0).margin(0.05));
    // Zero duration is a caller saying the length proves nothing.
    REQUIRE(refineTempo(173.4, 0.0, {}) == Approx(173.4));
}

TEST_CASE("Nothing measured is nothing resolved, whatever the hints say", "[media][tempo]") {
    REQUIRE_FALSE(resolveTempo(std::nullopt, 8.0, named(128.0)).has_value());
    REQUIRE_FALSE(hintAgrees(std::nullopt, named(128.0)));
}

TEST_CASE("A hint confirms a tempo the autocorrelation is unsure of", "[media][tempo]") {
    const auto unsure = measured(87.0, 0.2);
    REQUIRE(unsure.confidence < kMinTempoConfidence);

    REQUIRE(hintAgrees(unsure, named(174.0)));
    const auto confirmed = resolveTempo(unsure, 0.0, named(174.0));
    REQUIRE(confirmed.has_value());
    REQUIRE(*confirmed == Approx(174.0));

    REQUIRE_FALSE(hintAgrees(unsure, named(120.0)));
    REQUIRE_FALSE(resolveTempo(unsure, 0.0, named(120.0)).has_value());
    REQUIRE_FALSE(resolveTempo(unsure, 0.0, {}).has_value());
}

TEST_CASE("A confident measurement stands on its own", "[media][tempo]") {
    const auto sure = measured(128.0, 0.9);
    const auto resolved = resolveTempo(sure, 0.0, {});
    REQUIRE(resolved.has_value());
    REQUIRE(*resolved == Approx(128.0));

    const auto disagreed = resolveTempo(sure, 0.0, named(120.0));
    REQUIRE(disagreed.has_value());
    REQUIRE(*disagreed == Approx(128.0));
}

// A uniform 16th grid measures at a dotted-eighth period, two thirds of its
// tempo; the name lands on the 3:2 relation as it would on an octave.
TEST_CASE("A hint picks the 3:2 relation of the measured tempo", "[media_db][tempo][hints]") {
    magda::media::TempoHints byName;
    byName.fromName = 174.0;
    REQUIRE(magda::media::refineTempo(116.0, 0.0, byName) == Catch::Approx(174.0));
    magda::media::TempoHints lower;
    lower.fromName = 116.0;
    REQUIRE(magda::media::refineTempo(174.0, 0.0, lower) == Catch::Approx(116.0));
}
