#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "magda/agents/router_model.hpp"
#include "magda/daw/core/ConsoleRouting.hpp"
#include "router_model_parity_cases.hpp"

// Locks the hand-written C++ router backend to the Python POC reference: every
// committed test case must produce the same ConsoleIntent as the float32 Python
// forward (prototypes/router-model-poc/, issue #1843). Regenerate the fixture +
// weights via router/export_cpp.py.

using magda::RouterModel;
using namespace magda::routermodel::test;

TEST_CASE("Router model reproduces the Python classification", "[agents][router_model]") {
    RouterModel model;
    int mismatches = 0;
    for (int i = 0; i < kNumParityCases; ++i) {
        const auto& c = kParityCases[i];
        const std::string got = model.classify(c.input);
        if (got != c.expected) {
            ++mismatches;
            UNSCOPED_INFO("input:    " << c.input);
            UNSCOPED_INFO("expected: " << c.expected);
            UNSCOPED_INFO("got:      " << got);
        }
        CHECK(got == std::string(c.expected));
    }
    CHECK(mismatches == 0);
}

TEST_CASE("Router model emits only routable ConsoleIntent tokens", "[agents][router_model]") {
    // The label strings are the wire format between the model and
    // intentFromString(); a typo in the exported table would silently degrade
    // every classification to the Command fallback.
    RouterModel model;
    for (int i = 0; i < kNumParityCases; ++i) {
        const std::string got = model.classify(kParityCases[i].input);
        REQUIRE(!got.empty());
        CHECK(std::string(magda::toIntentString(magda::intentFromString(got))) == got);
    }
}

TEST_CASE("Router tokenizer handles the scripts the command model drops",
          "[agents][router_model]") {
    // The command model's ASCII tokenizer yields nothing for these, which is
    // why the router has its own. Regression guard: if this ever returns empty,
    // every non-Latin console turn silently falls back to the view default.
    RouterModel model;

    SECTION("CJK is one token per codepoint") {
        CHECK(model.tokenize("ドラム").size() == 3);
        CHECK(model.tokenize("创建轨道").size() == 4);
    }

    SECTION("Cyrillic forms whole-word runs") {
        const auto toks = model.tokenize("создай трек");
        REQUIRE(toks.size() == 2);
        CHECK(toks[0] == "создай");
        CHECK(toks[1] == "трек");
    }

    SECTION("mixed scripts and plugin sigils survive") {
        const auto toks = model.tokenize("ベースに@serumを追加");
        REQUIRE(toks.size() == 8);  // ベ ー ス に @serum を 追 加
        CHECK(toks[4] == "@serum");
    }

    SECTION("case folding covers Cyrillic, not just ASCII") {
        // fold() is what keeps an uppercase message from being all-<UNK>.
        CHECK(model.classify("СОЗДАЙ ТРЕК БАС") == model.classify("создай трек бас"));
        CHECK(model.classify("CREATE A BASS TRACK") == model.classify("create a bass track"));
    }

    SECTION("empty and punctuation-only input classify to nothing") {
        CHECK(model.classify("").empty());
        CHECK(model.classify("...   ").empty());
    }
}
