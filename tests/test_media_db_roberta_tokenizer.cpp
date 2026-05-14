// Phase D3 tests — RoBERTa BPE tokenizer (issue #768).
//
// Splits the tokenizer into pieces that can be tested without the 1.4 MB
// tokenizer.json. End-to-end encoding against the real file is gated on
// MAGDA_MEDIA_DB_TOKENIZER_JSON.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "../magda/daw/media_db/RobertaTokenizer.hpp"

namespace fs = std::filesystem;
using magda::media::RobertaTokenizer;
using magda::media::RobertaTokenizerError;

TEST_CASE("RobertaTokenizer throws on missing file", "[media_db][tokenizer]") {
    REQUIRE_THROWS_AS(RobertaTokenizer("/no/such/tokenizer.json"), RobertaTokenizerError);
}

TEST_CASE("RobertaTokenizer encodes from real tokenizer.json",
          "[media_db][tokenizer][needs-model]") {
    const char* envPath = std::getenv("MAGDA_MEDIA_DB_TOKENIZER_JSON");
    if (envPath == nullptr || !fs::exists(envPath)) {
        SKIP("set MAGDA_MEDIA_DB_TOKENIZER_JSON to a tokenizer.json to run this test");
    }

    RobertaTokenizer tok(envPath);

    // Sanity: RoBERTa-base has ~50k vocab + special tokens at expected IDs.
    REQUIRE(tok.vocabSize() > 40000);
    REQUIRE(tok.bosId() == 0);
    REQUIRE(tok.padId() == 1);
    REQUIRE(tok.eosId() == 2);
    REQUIRE(tok.unkId() == 3);

    SECTION("simple ASCII English query") {
        auto e = tok.encode("warm analog pad", 77);
        REQUIRE(e.inputIds.size() == 77);
        REQUIRE(e.attentionMask.size() == 77);
        REQUIRE(e.inputIds.front() == 0);  // BOS

        // Find EOS — first occurrence in inputIds. Everything after is PAD.
        std::size_t eosAt = 0;
        for (std::size_t i = 1; i < e.inputIds.size(); ++i) {
            if (e.inputIds[i] == 2) {
                eosAt = i;
                break;
            }
        }
        REQUIRE(eosAt > 1);
        REQUIRE(eosAt < 77);
        // Attention mask: 1s up to and including EOS, 0s after.
        for (std::size_t i = 0; i <= eosAt; ++i) {
            REQUIRE(e.attentionMask[i] == 1);
        }
        for (std::size_t i = eosAt + 1; i < 77; ++i) {
            REQUIRE(e.attentionMask[i] == 0);
            REQUIRE(e.inputIds[i] == 1);  // PAD
        }
    }

    SECTION("repeated query hits BPE cache") {
        // Both calls should produce identical outputs; cache hits don't affect
        // correctness, just performance.
        auto a = tok.encode("punchy kick drum");
        auto b = tok.encode("punchy kick drum");
        REQUIRE(a.inputIds == b.inputIds);
    }

    SECTION("truncates over-long input") {
        // Build a query that vastly exceeds 77 tokens.
        std::string text;
        for (int i = 0; i < 200; ++i) {
            text += "kick ";
        }
        auto e = tok.encode(text, 77);
        REQUIRE(e.inputIds.size() == 77);
        REQUIRE(e.inputIds.front() == 0);  // BOS preserved
        REQUIRE(e.inputIds.back() == 2);   // EOS preserved at the end (no PAD)
        // All attention positions should be on.
        for (int64_t m : e.attentionMask) {
            REQUIRE(m == 1);
        }
    }
}
