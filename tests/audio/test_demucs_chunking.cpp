// Demucs OLA geometry (issue #1288): the trapezoid window and chunk grid
// must match the StemSplitio export's reference inference (infer.py), since
// the weights were validated against exactly that stitching. These constants
// are compiled independently of the ONNX gate, so the math is testable on
// every platform.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "stem_separation/DemucsSeparator.hpp"

namespace demucs = magda::stems::demucs;

TEST_CASE("Demucs chunk constants match the export", "[stems][demucs]") {
    CHECK(demucs::kSampleRate == 44100);
    CHECK(demucs::kSegmentSamples == 343980);  // 7.8 s * 44100
    CHECK(demucs::kOverlapSamples == 85995);
    CHECK(demucs::kStrideSamples == 257985);
    CHECK(demucs::kNumStems == 4);
}

TEST_CASE("Trapezoid window shape", "[stems][demucs]") {
    const auto w = demucs::trapezoidWindow();
    REQUIRE(static_cast<int>(w.size()) == demucs::kSegmentSamples);

    // linspace(0, 1, overlap) endpoints.
    CHECK(w[0] == 0.0F);
    CHECK(w[static_cast<size_t>(demucs::kOverlapSamples) - 1] == 1.0F);

    // Flat top between the ramps.
    CHECK(w[static_cast<size_t>(demucs::kSegmentSamples) / 2] == 1.0F);
    CHECK(w[static_cast<size_t>(demucs::kOverlapSamples)] == 1.0F);

    // Symmetric fade-out.
    for (int i = 0; i < demucs::kOverlapSamples; i += 9999) {
        CHECK(w[static_cast<size_t>(i)] == w[static_cast<size_t>(demucs::kSegmentSamples - 1 - i)]);
    }
}

TEST_CASE("OLA weights cover a full track", "[stems][demucs]") {
    // Simulate the accumulation grid for a ~30 s track: every sample except
    // the very first (where linspace makes w[0] == 0) must receive positive
    // weight, and interior samples in overlap regions must sum to ~1 or more.
    const int total = demucs::kSampleRate * 30;
    const auto w = demucs::trapezoidWindow();

    std::vector<float> weight(static_cast<size_t>(total), 0.0F);
    const int numChunks = (total + demucs::kStrideSamples - 1) / demucs::kStrideSamples;
    for (int c = 0; c < numChunks; ++c) {
        const int start = c * demucs::kStrideSamples;
        const int clen = std::min(demucs::kSegmentSamples, total - start);
        for (int i = 0; i < clen; ++i)
            weight[static_cast<size_t>(start + i)] += w[static_cast<size_t>(i)];
    }

    int zeroWeight = 0;
    for (int i = 1; i < total; ++i) {
        if (weight[static_cast<size_t>(i)] <= 0.0F)
            ++zeroWeight;
    }
    CHECK(zeroWeight == 0);

    // Deep interior (past the first ramp, before the tail): weight ~1.
    for (int i = demucs::kSegmentSamples; i < total - demucs::kSegmentSamples; i += 100000) {
        CHECK(std::abs(weight[static_cast<size_t>(i)] - 1.0F) < 1.0e-3F);
    }
}
