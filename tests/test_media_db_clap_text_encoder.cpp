// Phase D2 tests — CLAP text encoder (issue #768).
//
// Same pattern as test_media_db_clap_encoder.cpp: most assertions exercise
// the surrounding plumbing (error on missing model, etc.); the end-to-end
// inference test is gated on MAGDA_MEDIA_DB_CLAP_TEXT_MODEL pointing at a
// real clap_text.onnx, since CI doesn't carry the 478 MB model.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "../magda/daw/media_db/ClapTextEncoder.hpp"

namespace fs = std::filesystem;
using magda::media::ClapTextEncoder;
using magda::media::ClapTextEncoderError;

TEST_CASE("ClapTextEncoder throws on missing model", "[media_db][clap][text]") {
    REQUIRE_THROWS_AS(ClapTextEncoder("/no/such/clap_text.onnx"), ClapTextEncoderError);
}

TEST_CASE("ClapTextEncoder rejects mismatched input lengths",
          "[media_db][clap][text][needs-model]") {
    const char* envPath = std::getenv("MAGDA_MEDIA_DB_CLAP_TEXT_MODEL");
    if (envPath == nullptr || !fs::exists(envPath)) {
        SKIP("set MAGDA_MEDIA_DB_CLAP_TEXT_MODEL to a clap_text.onnx to run this test");
    }
    ClapTextEncoder encoder(envPath);
    std::vector<int64_t> ids{0, 100, 200, 2};
    std::vector<int64_t> shortMask{1, 1, 1};
    REQUIRE_THROWS_AS(encoder.embedTokens(ids, shortMask), ClapTextEncoderError);

    std::vector<int64_t> empty;
    REQUIRE_THROWS_AS(encoder.embedTokens(empty, empty), ClapTextEncoderError);
}

TEST_CASE("ClapTextEncoder embeds tokens to a 512-dim normalized vector",
          "[media_db][clap][text][needs-model]") {
    const char* envPath = std::getenv("MAGDA_MEDIA_DB_CLAP_TEXT_MODEL");
    if (envPath == nullptr || !fs::exists(envPath)) {
        SKIP("set MAGDA_MEDIA_DB_CLAP_TEXT_MODEL to a clap_text.onnx to run this test");
    }

    ClapTextEncoder encoder(envPath);
    REQUIRE(encoder.dim() == 512);

    // RoBERTa BOS=0, EOS=2, PAD=1. A minimal valid input: just BOS+EOS, the
    // rest padding. The model doesn't care what the content is for this test
    // — we only check the output shape + normalization.
    constexpr int kSeqLen = 77;
    std::vector<int64_t> ids(kSeqLen, 1);   // pad
    std::vector<int64_t> mask(kSeqLen, 0);  // attention masked out
    ids[0] = 0;
    ids[1] = 2;
    mask[0] = 1;
    mask[1] = 1;

    auto emb = encoder.embedTokens(ids, mask);
    REQUIRE(emb.size() == 512);

    float sumSq = 0.0F;
    for (float v : emb) {
        sumSq += v * v;
    }
    REQUIRE(std::sqrt(sumSq) == Catch::Approx(1.0F).epsilon(0.001));
}
