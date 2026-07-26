// Spleeter ONNX backend (issue #1288): gate-free geometry tests plus an
// env-gated integration test against the real sherpa-onnx 2-stem models.
// Set MAGDA_STEM_SPLEETER_DIR to a directory holding vocals.onnx and
// accompaniment.onnx to run the model test.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numbers>

#include "stem_separation/SpleeterSeparator.hpp"

namespace sp = magda::stems::spleeter;

TEST_CASE("Spleeter DSP constants match the conversion", "[stems][spleeter]") {
    CHECK(sp::kSampleRate == 44100);
    CHECK(sp::kFftSize == 4096);
    CHECK(sp::kHop == 1024);
    CHECK(sp::kNumBins == 2049);
    CHECK(sp::kKeptBins == 1024);
    CHECK(sp::kFramesPerSplit == 512);
}

TEST_CASE("Frame padding rounds up to whole splits without spurious extra", "[stems][spleeter]") {
    CHECK(sp::paddedFrameCount(1) == 512);
    CHECK(sp::paddedFrameCount(511) == 512);
    CHECK(sp::paddedFrameCount(512) == 512);  // no extra split when aligned
    CHECK(sp::paddedFrameCount(513) == 1024);
    CHECK(sp::paddedFrameCount(1024) == 1024);
}

TEST_CASE("SpleeterSeparator separates a synthetic mix", "[stems][spleeter][needs-model]") {
    const char* envDir = std::getenv("MAGDA_STEM_SPLEETER_DIR");
    if (envDir == nullptr || !std::filesystem::exists(envDir)) {
        SKIP("set MAGDA_STEM_SPLEETER_DIR to a dir with vocals.onnx + accompaniment.onnx");
    }
    const auto dir = std::filesystem::path(envDir);

    magda::stems::SpleeterSeparator separator(dir / "vocals.onnx", dir / "accompaniment.onnx");
    REQUIRE(separator.isLoaded());

    // ~15 s stereo at 88.2 kHz spans two 512-frame splits after resampling.
    // The odd source length locks the bounded Lagrange path against over-read.
    // A 220 Hz tone
    // sits far below the 1024-bin cutoff, so the Wiener masks (which sum to
    // 1 by construction) must reconstruct it almost exactly.
    constexpr int kSr = 88200;
    constexpr int kLen = kSr * 15 + 1;
    juce::AudioBuffer<float> input(2, kLen);
    for (int ch = 0; ch < 2; ++ch) {
        float* data = input.getWritePointer(ch);
        for (int i = 0; i < kLen; ++i)
            data[i] = 0.4F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / kSr));
    }

    float lastProgress = -1.0F;
    auto stems = separator.separate(input, kSr, [&](float frac) {
        lastProgress = frac;
        return true;
    });

    REQUIRE(stems.size() == 2);
    CHECK(stems[0].name == "Vocals");
    CHECK(stems[1].name == "Accompaniment");
    CHECK(lastProgress == 1.0F);
    for (const auto& s : stems) {
        REQUIRE(s.audio.getNumChannels() == 2);
        REQUIRE(s.audio.getNumSamples() == kLen);
    }

    // Interior samples (skip a second at each edge): vocals + accompaniment
    // must sum back to the input.
    const int from = kSr;
    const int to = kLen - kSr;
    double sumSq = 0.0;
    double errSq = 0.0;
    const float* in = input.getReadPointer(0);
    const float* v = stems[0].audio.getReadPointer(0);
    const float* a = stems[1].audio.getReadPointer(0);
    for (int i = from; i < to; ++i) {
        const double e = static_cast<double>(in[i]) - (v[i] + a[i]);
        sumSq += static_cast<double>(in[i]) * in[i];
        errSq += e * e;
    }
    REQUIRE(sumSq > 0.0);
    CHECK(std::sqrt(errSq / sumSq) < 0.05);  // < 5% relative RMS error
}

TEST_CASE("Spleeter cancellation returns empty", "[stems][spleeter][needs-model]") {
    const char* envDir = std::getenv("MAGDA_STEM_SPLEETER_DIR");
    if (envDir == nullptr || !std::filesystem::exists(envDir)) {
        SKIP("set MAGDA_STEM_SPLEETER_DIR to a dir with vocals.onnx + accompaniment.onnx");
    }
    const auto dir = std::filesystem::path(envDir);

    magda::stems::SpleeterSeparator separator(dir / "vocals.onnx", dir / "accompaniment.onnx");
    REQUIRE(separator.isLoaded());

    juce::AudioBuffer<float> input(1, 44100);
    input.clear();
    auto stems = separator.separate(input, 44100.0, [](float) { return false; });
    CHECK(stems.empty());
}
