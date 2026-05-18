// Phase D tests — CLAP audio encoder + mel preprocessing (issue #768).
//
// The encoder needs a real ONNX file on disk to actually run inference, so
// most of this file exercises the pieces around that: mel filterbank shape,
// log-mel output dimensions, error handling on missing model. The end-to-end
// inference test is gated on MAGDA_MEDIA_DB_CLAP_MODEL pointing at a real
// clap_audio.onnx (the same one media-db export-onnx produces in the Python
// prototype), so CI doesn't bake in a 112 MB checked-in model.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <vector>

#include "../magda/daw/media_db/ClapAudioEncoder.hpp"
#include "../magda/daw/media_db/MelSpectrogram.hpp"

namespace fs = std::filesystem;
using magda::media::ClapAudioEncoder;
using magda::media::ClapEncoderError;
using magda::media::MelConfig;

TEST_CASE("mel filterbank shape matches CLAP config", "[media_db][clap][mel]") {
    MelConfig cfg;
    auto fb = magda::media::buildMelFilterbank(cfg);
    REQUIRE(fb.size() == static_cast<size_t>(cfg.nMels) * (cfg.nFft / 2 + 1));

    // Sanity: every row should have some non-zero weight (each filter spans
    // a triangular region) and the peak should be ≤ 1 with no normalization.
    for (int m = 0; m < cfg.nMels; ++m) {
        const float* row = &fb[static_cast<size_t>(m) * (cfg.nFft / 2 + 1)];
        float maxW = 0.0F;
        for (int k = 0; k < cfg.nFft / 2 + 1; ++k) {
            maxW = std::max(maxW, row[k]);
        }
        REQUIRE(maxW > 0.0F);
        REQUIRE(maxW <= 1.001F);
    }
}

TEST_CASE("computeLogMel returns expected shape", "[media_db][clap][mel]") {
    MelConfig cfg;
    std::vector<float> silence(cfg.targetSamples, 0.0F);
    auto mel = magda::media::computeLogMel(silence.data(), cfg.targetSamples, cfg);

    const int expectedFrames = cfg.targetSamples / cfg.hopLength + 1;
    REQUIRE(mel.size() == static_cast<size_t>(cfg.nMels) * expectedFrames);

    // Log of (filterbank · 0 + eps) collapses to log(eps); should be uniform
    // across all bins for true silence.
    for (float v : mel) {
        REQUIRE(v == Catch::Approx(std::log(1e-10F)).epsilon(0.01));
    }
}

TEST_CASE("computeLogMel produces signal-shaped output on a sine", "[media_db][clap][mel]") {
    MelConfig cfg;
    std::vector<float> sine(cfg.targetSamples);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    const double freq = 1000.0;
    for (int i = 0; i < cfg.targetSamples; ++i) {
        sine[i] = static_cast<float>(0.5 * std::sin(kTwoPi * freq * i / cfg.sampleRate));
    }

    auto mel = magda::media::computeLogMel(sine.data(), cfg.targetSamples, cfg);

    // For a pure tone, at least one mel bin must show real energy well above
    // the silence floor (log(1e-10) ≈ -23). Without specifying which bin —
    // mel scale is logarithmic so 1 kHz lands somewhere around index 17, not
    // the linearly-spaced position you'd guess.
    float maxVal = -1e30F;
    for (float v : mel) {
        maxVal = std::max(maxVal, v);
    }
    REQUIRE(maxVal > 0.0F);
}

TEST_CASE("ClapAudioEncoder throws on missing model", "[media_db][clap]") {
    REQUIRE_THROWS_AS(ClapAudioEncoder("/no/such/clap_audio.onnx"), ClapEncoderError);
}

TEST_CASE("ClapAudioEncoder embeds a sine to a 512-dim normalized vector",
          "[media_db][clap][needs-model]") {
    const char* envPath = std::getenv("MAGDA_MEDIA_DB_CLAP_MODEL");
    if (envPath == nullptr || !fs::exists(envPath)) {
        SKIP("set MAGDA_MEDIA_DB_CLAP_MODEL to a clap_audio.onnx to run this test");
    }

    ClapAudioEncoder encoder(envPath);
    REQUIRE(encoder.dim() == 512);
    REQUIRE(encoder.sampleRate() == 48000);

    constexpr int kSr = 48000;
    constexpr int kDur = 3;
    std::vector<float> sine(kSr * kDur);
    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    for (int i = 0; i < static_cast<int>(sine.size()); ++i) {
        sine[i] = static_cast<float>(0.3 * std::sin(kTwoPi * 440.0 * i / kSr));
    }

    auto emb = encoder.embed(sine.data(), static_cast<int>(sine.size()));
    REQUIRE(emb.size() == 512);

    // L2-normalized
    float sumSq = 0.0F;
    for (float v : emb) {
        sumSq += v * v;
    }
    REQUIRE(std::sqrt(sumSq) == Catch::Approx(1.0F).epsilon(0.001));
}
