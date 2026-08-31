#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "magda/agents/unigram_tokenizer.hpp"

// Locks the hand-written Unigram tokenizer to the Hugging Face reference over
// the corpus in tests/unigram_tokenizer_cases.json (regenerate with
// prototypes/command-model-poc/model/export_tokenizer_cases.py).
//
// The DSL-level parity fixture cannot substitute for this: a one-subword shift
// still produces plausible-looking DSL most of the time, so tokenizer drift
// hides as a handful of "model got it wrong" cases rather than as a failure.
//
// tokenizer.json is ~9 MB and is a build artifact, so it is not committed.
// Point MAGDA_COMMAND_TOKENIZER at it to run these; otherwise they skip, the
// same way the Spleeter tests do without their models.

using magda::cmdmodel::UnigramTokenizer;

namespace {

std::filesystem::path tokenizerPath() {
    if (const char* env = std::getenv("MAGDA_COMMAND_TOKENIZER"))
        return {env};
    return {};
}

std::filesystem::path casesPath() {
    return std::filesystem::path(__FILE__).parent_path() / "unigram_tokenizer_cases.json";
}

}  // namespace

TEST_CASE("Unigram tokenizer mirrors the Hugging Face reference", "[agents][tokenizer]") {
    const auto tokPath = tokenizerPath();
    if (tokPath.empty() || !std::filesystem::exists(tokPath)) {
        SKIP("set MAGDA_COMMAND_TOKENIZER to the exported tokenizer.json");
    }
    REQUIRE(std::filesystem::exists(casesPath()));

    UnigramTokenizer tok(tokPath);

    auto json = juce::JSON::parse(juce::File(juce::String(casesPath().string())));
    REQUIRE_FALSE(json.isVoid());
    const auto* cases = json.getProperty("cases", juce::var()).getArray();
    REQUIRE(cases != nullptr);

    int checked = 0;
    int mismatches = 0;
    for (const auto& c : *cases) {
        auto* obj = c.getDynamicObject();
        if (obj == nullptr)
            continue;
        const auto word = obj->getProperty("word").toString().toStdString();
        const auto* expectedPieces = obj->getProperty("pieces").getArray();
        if (expectedPieces == nullptr)
            continue;

        std::vector<std::string> expected;
        expected.reserve(static_cast<size_t>(expectedPieces->size()));
        for (const auto& p : *expectedPieces)
            expected.push_back(p.toString().toStdString());

        const auto got = tok.pieces(word);
        ++checked;
        if (got != expected) {
            ++mismatches;
            if (mismatches <= 10) {  // don't drown the output on a systemic break
                std::string e, g;
                for (const auto& s : expected)
                    e += "[" + s + "]";
                for (const auto& s : got)
                    g += "[" + s + "]";
                UNSCOPED_INFO("word: " << word);
                UNSCOPED_INFO("  expected " << e);
                UNSCOPED_INFO("  got      " << g);
            }
        }
    }

    UNSCOPED_INFO("checked " << checked << " words, " << mismatches << " mismatches");
    CHECK(checked > 3000);
    CHECK(mismatches == 0);
}

TEST_CASE("Unigram tokenizer wraps words with CLS/SEP and tracks first subwords",
          "[agents][tokenizer]") {
    const auto tokPath = tokenizerPath();
    if (tokPath.empty() || !std::filesystem::exists(tokPath)) {
        SKIP("set MAGDA_COMMAND_TOKENIZER to the exported tokenizer.json");
    }
    UnigramTokenizer tok(tokPath);

    // From the Python reference:
    //   ['create','a','bass','track','with','<alias>']
    //     -> [CLS] ▁create ▁a ▁bass ▁track ▁with <alias> [SEP]
    //     -> ids  1 676 266 5839 1362 275 128001 2
    const std::vector<std::string> words = {"create", "a", "bass", "track", "with", "<alias>"};
    const auto enc = tok.encodeWords(words, 64);

    CHECK(enc.inputIds == std::vector<int64_t>{1, 676, 266, 5839, 1362, 275, 128001, 2});
    CHECK(enc.attentionMask.size() == enc.inputIds.size());
    // One word per subword here, so each word starts at 1 + its index.
    CHECK(enc.firstSubword == std::vector<int>{1, 2, 3, 4, 5, 6});

    // "Krunchy" splits into three pieces, so the following word's read-out
    // index must shift by three — the alignment bug this test exists to catch.
    const auto multi = tok.encodeWords({"Krunchy", "Kick"}, 64);
    CHECK(multi.inputIds == std::vector<int64_t>{1, 1148, 5571, 20623, 20994, 2});
    CHECK(multi.firstSubword == std::vector<int>{1, 4});

    // Truncated words report -1 so the caller can fall back to "O".
    const std::vector<std::string> many(40, "supercalifragilistic");
    const auto trunc = tok.encodeWords(many, 16);
    CHECK(trunc.inputIds.size() <= 16);
    CHECK(trunc.firstSubword.back() == -1);
}
