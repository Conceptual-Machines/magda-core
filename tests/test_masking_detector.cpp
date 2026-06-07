#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../magda/daw/audio/analysis/MaskingDetector.hpp"

using magda::daw::audio::detectMasking;
using magda::daw::audio::kNumMaskingBands;
using magda::daw::audio::maskingBandEdgeHz;
using magda::daw::audio::MaskingOptions;
using magda::daw::audio::TrackBandEnergies;

namespace {

// A track silent everywhere except the given (band -> dB) peaks.
TrackBandEnergies makeTrack(int id, const char* name,
                            std::initializer_list<std::pair<int, float>> peaks) {
    TrackBandEnergies t;
    t.trackId = id;
    t.name = name;
    t.bandDb.fill(-80.0f);
    for (auto [band, db] : peaks)
        t.bandDb[static_cast<size_t>(band)] = db;
    return t;
}

}  // namespace

TEST_CASE("MaskingDetector - band edges span the audio range", "[masking]") {
    REQUIRE(maskingBandEdgeHz(0) == Catch::Approx(20.0f));
    REQUIRE(maskingBandEdgeHz(kNumMaskingBands) == Catch::Approx(20480.0f).margin(1.0f));
}

TEST_CASE("MaskingDetector - two tracks sharing a band are flagged", "[masking]") {
    std::vector<TrackBandEnergies> tracks = {makeTrack(1, "Kick", {{16, 0.0f}}),
                                             makeTrack(2, "Bass", {{16, 0.0f}})};
    auto findings = detectMasking(tracks);
    REQUIRE(findings.size() == 1);
    REQUIRE(findings[0].trackA == 1);
    REQUIRE(findings[0].trackB == 2);
    REQUIRE(findings[0].severity > 0.5f);
    REQUIRE(findings[0].loHz == Catch::Approx(maskingBandEdgeHz(16)));
    REQUIRE(findings[0].hiHz == Catch::Approx(maskingBandEdgeHz(17)));
}

TEST_CASE("MaskingDetector - disjoint spectra produce no findings", "[masking]") {
    std::vector<TrackBandEnergies> tracks = {makeTrack(1, "Sub", {{4, 0.0f}}),
                                             makeTrack(2, "Air", {{27, 0.0f}})};
    REQUIRE(detectMasking(tracks).empty());
}

TEST_CASE("MaskingDetector - single track never masks", "[masking]") {
    std::vector<TrackBandEnergies> tracks = {makeTrack(1, "Solo", {{10, 0.0f}, {16, 0.0f}})};
    REQUIRE(detectMasking(tracks).empty());
}

TEST_CASE("MaskingDetector - stronger overlap ranks above weaker", "[masking]") {
    // A and B both peak in band 16; C only weakly present there (-10 dB).
    std::vector<TrackBandEnergies> tracks = {makeTrack(1, "A", {{16, 0.0f}}),
                                             makeTrack(2, "B", {{16, 0.0f}}),
                                             makeTrack(3, "C", {{16, -10.0f}})};
    auto findings = detectMasking(tracks);
    REQUIRE(findings.size() >= 1);
    // Worst finding is the full A/B overlap.
    REQUIRE(((findings[0].trackA == 1 && findings[0].trackB == 2)));
    // Any A/C or B/C finding is weaker than A/B.
    for (size_t i = 1; i < findings.size(); ++i)
        REQUIRE(findings[i].severity <= findings[0].severity);
}

TEST_CASE("MaskingDetector - adjacent shared bands merge into one range", "[masking]") {
    std::vector<TrackBandEnergies> tracks = {
        makeTrack(1, "A", {{15, 0.0f}, {16, 0.0f}, {17, 0.0f}}),
        makeTrack(2, "B", {{15, 0.0f}, {16, 0.0f}, {17, 0.0f}})};
    auto findings = detectMasking(tracks);
    REQUIRE(findings.size() == 1);
    REQUIRE(findings[0].loHz == Catch::Approx(maskingBandEdgeHz(15)));
    REQUIRE(findings[0].hiHz == Catch::Approx(maskingBandEdgeHz(18)));
}

TEST_CASE("MaskingDetector - maxFindings caps the list", "[masking]") {
    std::vector<TrackBandEnergies> tracks;
    for (int i = 0; i < 6; ++i)
        tracks.push_back(makeTrack(i + 1, "T", {{16, 0.0f}}));  // all mutually mask in band 16
    MaskingOptions opts;
    opts.maxFindings = 3;
    auto findings = detectMasking(tracks, opts);
    REQUIRE(findings.size() == 3);
}

TEST_CASE("MaskingDetector - content below the floor is ignored", "[masking]") {
    std::vector<TrackBandEnergies> tracks = {makeTrack(1, "A", {{16, -70.0f}}),
                                             makeTrack(2, "B", {{16, -70.0f}})};
    REQUIRE(detectMasking(tracks).empty());
}
