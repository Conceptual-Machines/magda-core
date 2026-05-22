// Issue #1319 tests — CLAP zero-shot tagger.
//
// Pure-function paths (familyForPrompt, familyFromTopTags, prompt list
// shape) run unconditionally; the matrix-build path needs the text encoder
// + tokenizer so it's gated on MAGDA_MEDIA_DB_CLAP_TEXT_MODEL and
// MAGDA_MEDIA_DB_TOKENIZER_JSON, matching the convention used by the
// existing clap encoder / tokenizer tests.

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <vector>

#include "../magda/daw/media_db/ClapTextEncoder.hpp"
#include "../magda/daw/media_db/MediaDbZeroShotTags.hpp"
#include "../magda/daw/media_db/RobertaTokenizer.hpp"

namespace fs = std::filesystem;
using magda::media::ClapTextEncoder;
using magda::media::defaultZeroShotPrompts;
using magda::media::familyForPrompt;
using magda::media::familyFromTopTags;
using magda::media::kZeroShotFamilyFloor;
using magda::media::RobertaTokenizer;
using magda::media::ZeroShotTagger;

TEST_CASE("default prompt list covers expected families", "[media_db][zero-shot]") {
    const auto& prompts = defaultZeroShotPrompts();
    REQUIRE(prompts.size() >= 30);

    // Spot-check the families we care about most. The set must be a
    // superset of these — concrete prompt strings can change without
    // breaking the test as long as the family map keeps producing the
    // expected coarse buckets.
    std::set<std::string> families;
    for (const auto& p : prompts) {
        families.insert(familyForPrompt(p));
    }
    for (const auto& expected : {"drum", "bass", "lead", "pad", "keys", "guitar", "orchestral",
                                 "vocal", "fx", "texture"}) {
        REQUIRE(families.count(expected) == 1);
    }
}

TEST_CASE("familyForPrompt returns unknown for strings not in the map", "[media_db][zero-shot]") {
    REQUIRE(familyForPrompt("not a real prompt") == "unknown");
    REQUIRE(familyForPrompt("") == "unknown");
}

TEST_CASE("familyFromTopTags picks the top non-texture instrument tag", "[media_db][zero-shot]") {
    using V = std::vector<std::pair<std::string, float>>;

    SECTION("top instrument tag wins") {
        V tags{
            {"the sound of a synth pad", 0.42F},
            {"the sound of a warm sound", 0.30F},
        };
        REQUIRE(familyFromTopTags(tags) == "pad");
    }

    SECTION("texture is skipped even when it scores higher") {
        // Texture is sorted first because score is higher, but the function
        // must walk past it to find the real instrument family.
        V tags{
            {"the sound of a warm sound", 0.55F},
            {"the sound of a synth pad", 0.40F},
        };
        REQUIRE(familyFromTopTags(tags) == "pad");
    }

    SECTION("returns empty when nothing clears the floor") {
        V tags{
            {"the sound of a synth pad", kZeroShotFamilyFloor - 0.01F},
            {"the sound of a kick drum", 0.05F},
        };
        REQUIRE(familyFromTopTags(tags).empty());
    }

    SECTION("returns empty when only texture tags clear the floor") {
        V tags{
            {"the sound of a warm sound", 0.40F},
            {"the sound of a dark sound", 0.30F},
        };
        REQUIRE(familyFromTopTags(tags).empty());
    }

    SECTION("empty input returns empty") {
        REQUIRE(familyFromTopTags({}).empty());
    }

    SECTION("unknown-family tags are skipped") {
        V tags{
            {"unknown prompt not in map", 0.90F},
            {"the sound of a vocal", 0.30F},
        };
        REQUIRE(familyFromTopTags(tags) == "vocal");
    }

    SECTION("first non-texture above floor wins, not the absolute top") {
        // Below-floor instrument tag is ignored even though it's first.
        V tags{
            {"the sound of a kick drum", kZeroShotFamilyFloor - 0.001F},
            {"the sound of a vocal", kZeroShotFamilyFloor + 0.05F},
        };
        // Sorted descending — kick is "first" but below floor, so we stop.
        // (This matches the prototype's behaviour: the floor short-circuits
        // the walk before later candidates can be considered.)
        REQUIRE(familyFromTopTags(tags).empty());
    }
}

TEST_CASE("ZeroShotTagger builds prompt matrix and scores audio embeddings",
          "[media_db][zero-shot][needs-model]") {
    const char* textModelPath = std::getenv("MAGDA_MEDIA_DB_CLAP_TEXT_MODEL");
    const char* tokenizerPath = std::getenv("MAGDA_MEDIA_DB_TOKENIZER_JSON");
    if (textModelPath == nullptr || !fs::exists(textModelPath) || tokenizerPath == nullptr ||
        !fs::exists(tokenizerPath)) {
        SKIP("set MAGDA_MEDIA_DB_CLAP_TEXT_MODEL + MAGDA_MEDIA_DB_TOKENIZER_JSON to run this test");
    }

    ClapTextEncoder textEncoder(textModelPath);
    RobertaTokenizer tokenizer(tokenizerPath);

    ZeroShotTagger tagger(textEncoder, tokenizer);
    REQUIRE(tagger.numPrompts() == defaultZeroShotPrompts().size());
    REQUIRE(tagger.embeddingDim() == 512);

    // Score a zero vector — every cosine is 0, so the tagger should emit no
    // tags at the default threshold. This exercises the scoring math
    // independently of which audio CLAP would actually recognize.
    std::vector<float> zeros(tagger.embeddingDim(), 0.0F);
    const auto hitsAtThreshold = tagger.scoreEmbedding(zeros.data(), zeros.size());
    REQUIRE(hitsAtThreshold.empty());

    // Same vector with a negative threshold returns every prompt — every
    // cosine is 0 which exceeds -1.
    const auto allHits = tagger.scoreEmbedding(zeros.data(), zeros.size(), -1.0F);
    REQUIRE(allHits.size() == tagger.numPrompts());

    // Take a prompt embedding and use it as the "audio" embedding — its
    // cosine with itself must be 1.0 (within float tolerance) and the
    // prompt's own string must appear first in the sorted hits.
    const auto enc = tokenizer.encode("the sound of a kick drum");
    auto kickVec = textEncoder.embedTokens(enc.inputIds, enc.attentionMask);
    REQUIRE(kickVec.size() == tagger.embeddingDim());

    const auto hits = tagger.scoreEmbedding(kickVec.data(), kickVec.size(), 0.0F);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits.front().first == "the sound of a kick drum");
    REQUIRE(hits.front().second == Catch::Approx(1.0F).margin(1e-3));
}

TEST_CASE("ZeroShotTagger rejects mismatched embedding dims",
          "[media_db][zero-shot][needs-model]") {
    const char* textModelPath = std::getenv("MAGDA_MEDIA_DB_CLAP_TEXT_MODEL");
    const char* tokenizerPath = std::getenv("MAGDA_MEDIA_DB_TOKENIZER_JSON");
    if (textModelPath == nullptr || !fs::exists(textModelPath) || tokenizerPath == nullptr ||
        !fs::exists(tokenizerPath)) {
        SKIP("set MAGDA_MEDIA_DB_CLAP_TEXT_MODEL + MAGDA_MEDIA_DB_TOKENIZER_JSON to run this test");
    }

    ClapTextEncoder textEncoder(textModelPath);
    RobertaTokenizer tokenizer(tokenizerPath);
    ZeroShotTagger tagger(textEncoder, tokenizer);

    std::vector<float> wrongDim(tagger.embeddingDim() / 2, 0.0F);
    REQUIRE_THROWS(tagger.scoreEmbedding(wrongDim.data(), wrongDim.size()));
}
