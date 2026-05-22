// Issue #1319 tests — CLAP zero-shot tagger.
//
// Pure-function paths (familyForLabel, familyFromTopLabels, label list
// shape) run unconditionally; the matrix-build path needs the text encoder
// + tokenizer so it's gated on MAGDA_MEDIA_DB_CLAP_TEXT_MODEL and
// MAGDA_MEDIA_DB_TOKENIZER_JSON, matching the convention used by the
// existing clap encoder / tokenizer tests.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "../magda/daw/media_db/ClapTextEncoder.hpp"
#include "../magda/daw/media_db/MediaDbZeroShotTags.hpp"
#include "../magda/daw/media_db/RobertaTokenizer.hpp"

namespace fs = std::filesystem;
using magda::media::ClapTextEncoder;
using magda::media::defaultZeroShotLabels;
using magda::media::familyForLabel;
using magda::media::familyFromTopLabels;
using magda::media::kZeroShotFamilyFloor;
using magda::media::RobertaTokenizer;
using magda::media::ZeroShotTagger;

TEST_CASE("default label list is terse and covers expected families", "[media_db][zero-shot]") {
    const auto& labels = defaultZeroShotLabels();
    REQUIRE(labels.size() >= 30);

    // Labels must NOT carry the "the sound of " prompt wrapper — that
    // wrapping is an internal detail of the embedding step, and storing the
    // wrapped form would pollute media_tag and the FTS index. Asserting
    // this directly so future taxonomy edits don't accidentally reintroduce
    // the wrapper.
    for (const auto& label : labels) {
        REQUIRE(label.find("the sound of ") == std::string::npos);
    }

    // Spot-check the families we care about most. The set must be a
    // superset of these — concrete label strings can change without
    // breaking the test as long as the family map keeps producing the
    // expected coarse buckets.
    std::set<std::string> families;
    for (const auto& label : labels) {
        families.insert(familyForLabel(label));
    }
    for (const auto& expected : {"drum", "bass", "lead", "pad", "keys", "guitar", "orchestral",
                                 "vocal", "fx", "texture"}) {
        REQUIRE(families.count(expected) == 1);
    }
}

TEST_CASE("familyForLabel returns unknown for strings not in the map", "[media_db][zero-shot]") {
    REQUIRE(familyForLabel("not a real label") == "unknown");
    REQUIRE(familyForLabel("") == "unknown");
    // The wrapped prompt form is explicitly NOT in the map — a regression
    // here would mean someone re-keyed the map on prompts instead of labels.
    REQUIRE(familyForLabel("the sound of a kick drum") == "unknown");
}

TEST_CASE("familyFromTopLabels picks the top non-texture instrument tag", "[media_db][zero-shot]") {
    using V = std::vector<std::pair<std::string, float>>;

    SECTION("top instrument label wins") {
        V tags{
            {"a synth pad", 0.42F},
            {"a warm sound", 0.30F},
        };
        REQUIRE(familyFromTopLabels(tags) == "pad");
    }

    SECTION("texture is skipped even when it scores higher") {
        // Texture is sorted first because score is higher, but the function
        // must walk past it to find the real instrument family.
        V tags{
            {"a warm sound", 0.55F},
            {"a synth pad", 0.40F},
        };
        REQUIRE(familyFromTopLabels(tags) == "pad");
    }

    SECTION("returns empty when nothing clears the floor") {
        V tags{
            {"a synth pad", kZeroShotFamilyFloor - 0.01F},
            {"a kick drum", 0.05F},
        };
        REQUIRE(familyFromTopLabels(tags).empty());
    }

    SECTION("returns empty when only texture labels clear the floor") {
        V tags{
            {"a warm sound", 0.40F},
            {"a dark sound", 0.30F},
        };
        REQUIRE(familyFromTopLabels(tags).empty());
    }

    SECTION("empty input returns empty") {
        REQUIRE(familyFromTopLabels({}).empty());
    }

    SECTION("unknown-family labels are skipped") {
        V tags{
            {"unknown label not in map", 0.90F},
            {"a vocal", 0.30F},
        };
        REQUIRE(familyFromTopLabels(tags) == "vocal");
    }

    SECTION("first non-texture above floor wins, not the absolute top") {
        // Below-floor instrument label is ignored even though it's first.
        V tags{
            {"a kick drum", kZeroShotFamilyFloor - 0.001F},
            {"a vocal", kZeroShotFamilyFloor + 0.05F},
        };
        // Sorted descending — kick is "first" but below floor, so we stop.
        // (Matches the prototype's behaviour: the floor short-circuits the
        // walk before later candidates can be considered.)
        REQUIRE(familyFromTopLabels(tags).empty());
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
    REQUIRE(tagger.numLabels() == defaultZeroShotLabels().size());
    REQUIRE(tagger.embeddingDim() == 512);

    // Score a zero vector — every cosine is 0, so the tagger should emit no
    // tags at the default threshold. Exercises the scoring math
    // independently of which audio CLAP would actually recognize.
    std::vector<float> zeros(tagger.embeddingDim(), 0.0F);
    const auto hitsAtThreshold = tagger.scoreEmbedding(zeros.data(), zeros.size());
    REQUIRE(hitsAtThreshold.empty());

    // Same vector with a negative threshold returns every label — every
    // cosine is 0 which exceeds -1.
    const auto allHits = tagger.scoreEmbedding(zeros.data(), zeros.size(), -1.0F);
    REQUIRE(allHits.size() == tagger.numLabels());

    // Take the wrapped prompt embedding for "a kick drum" and use it as
    // the "audio" embedding — its cosine with itself must be ~1.0 and the
    // returned label must be the terse form (not the wrapped prompt).
    const auto enc = tokenizer.encode("the sound of a kick drum");
    auto kickVec = textEncoder.embedTokens(enc.inputIds, enc.attentionMask);
    REQUIRE(kickVec.size() == tagger.embeddingDim());

    const auto hits = tagger.scoreEmbedding(kickVec.data(), kickVec.size(), 0.0F);
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits.front().first == "a kick drum");
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
