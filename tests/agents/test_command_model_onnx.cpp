#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "magda/agents/command_model_onnx.hpp"

// Locks the ONNX command-model backend to the Python reference, the way
// test_command_model.cpp does for the conv net. Cases come from
// prototypes/command-model-poc/model/export_onnx.py, which writes
// parity_cases.json holding what the Python model produced for every committed
// evaluation row (testset + ood + dev).
//
// The fixture records what the MODEL produced, not the gold DSL: a parity test
// must fail on C++/Python divergence, not on the model being wrong about a
// sentence. Model quality is measured in the POC, by eval.run.
//
// Assets are a ~736 MB download and are not committed. Point
// MAGDA_COMMAND_MODEL_DIR at an export directory to run this; otherwise it
// skips, the same way the Spleeter tests do.

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

using magda::CommandModelOnnx;

namespace {

std::filesystem::path assetDir() {
    if (const char* env = std::getenv("MAGDA_COMMAND_MODEL_DIR"))
        return {env};
    return {};
}

std::filesystem::path casesPath() {
    const auto dir = assetDir();
    return dir.empty() ? std::filesystem::path{} : dir / "parity_cases.json";
}

}  // namespace

TEST_CASE("ONNX command model reproduces the Python DSL byte-for-byte",
          "[agents][command_model][onnx]") {
    const auto dir = assetDir();
    if (dir.empty() || !CommandModelOnnx::isInstalled(dir)) {
        SKIP("set MAGDA_COMMAND_MODEL_DIR to an export from model/export_onnx.py");
    }
    REQUIRE(std::filesystem::exists(casesPath()));

    CommandModelOnnx model(dir);

    auto json = juce::JSON::parse(juce::File(juce::String(casesPath().string())));
    REQUIRE_FALSE(json.isVoid());
    const auto* cases = json.getArray();
    REQUIRE(cases != nullptr);

    int checked = 0;
    int mismatches = 0;
    for (const auto& c : *cases) {
        auto* obj = c.getDynamicObject();
        if (obj == nullptr)
            continue;
        const auto input = obj->getProperty("input").toString().toStdString();
        const auto expected = obj->getProperty("expected").toString().toStdString();

        const auto got = model.generate(input);
        ++checked;
        if (got != expected) {
            ++mismatches;
            if (mismatches <= 10) {
                UNSCOPED_INFO("input:    " << input);
                UNSCOPED_INFO("expected: " << expected);
                UNSCOPED_INFO("got:      " << got);
            }
        }
    }

    UNSCOPED_INFO("checked " << checked << " cases, " << mismatches << " mismatches");
    // Every fixture row must actually run — a literal expected count goes stale
    // whenever the eval sets change (removing intents shrank them from 231 to
    // 216), which turns a drift guard into a maintenance chore. The floor only
    // catches a fixture that emptied.
    CHECK(checked == static_cast<int>(cases->size()));
    CHECK(checked > 150);
    CHECK(mismatches == 0);
}

TEST_CASE("ONNX command model tags words, not subwords", "[agents][command_model][onnx]") {
    const auto dir = assetDir();
    if (dir.empty() || !CommandModelOnnx::isInstalled(dir)) {
        SKIP("set MAGDA_COMMAND_MODEL_DIR to an export from model/export_onnx.py");
    }
    CommandModelOnnx model(dir);

    // A name that splits into several subwords: if tags were read positionally
    // instead of at each word's first subword, everything after "Krunchy"
    // would shift and the track name would come out wrong.
    const auto p = model.predict("add a track and name it Krunchy Kick");
    CHECK(p.tags.size() == p.tokens.size());
    CHECK(model.generate("add a track and name it Krunchy Kick") ==
          "track(name=\"Krunchy Kick\", new=true)");

    // The failure #1847 opens with: an unnamed track carrying a device must
    // create a track, not put a rack on the selected one.
    CHECK(model.generate("track with @fm_0") ==
          "track(name=\"\", new=true).fx.add(name=\"<fm_0>\")");
}

#endif  // MAGDA_HAVE_CLAP

// maps.json validation lives outside the CLAP guard (like defaultAssetDir),
// so these run on every build and need no model bundle. A model trained
// against one id table and decoded through another mislabels every
// prediction, so a maps.json whose ids are not a dense 0..n-1 range must be
// rejected at load, not silently reindexed.
TEST_CASE("Command model maps.json ids must form a dense 0..n-1 range",
          "[agents][command_model][onnx]") {
    using magda::CommandModelOnnxError;
    using magda::parseCommandModelMaps;

    // Wrap one intents object with a minimal valid tags object, so each
    // section exercises exactly one violation.
    const auto withIntents = [](const std::string& intents) {
        return "{\"intents\": " + intents + ", \"tags\": {\"O\": 0}}";
    };

    SECTION("valid maps invert to dense id-indexed tables") {
        const auto maps =
            parseCommandModelMaps("{\"intents\": {\"set_track_volume\": 1, \"create_track\": 0},"
                                  " \"tags\": {\"O\": 0, \"B-VALUE\": 2, \"B-TRACK_NAME\": 1}}");
        CHECK(maps.idToIntent == std::vector<std::string>{"create_track", "set_track_volume"});
        CHECK(maps.idToTag == std::vector<std::string>{"O", "B-TRACK_NAME", "B-VALUE"});
    }
    SECTION("negative id") {
        CHECK_THROWS_AS(parseCommandModelMaps(withIntents("{\"a\": -1, \"b\": 0}")),
                        CommandModelOnnxError);
    }
    SECTION("id at or beyond the map size") {
        CHECK_THROWS_AS(parseCommandModelMaps(withIntents("{\"a\": 0, \"b\": 2}")),
                        CommandModelOnnxError);
    }
    SECTION("duplicate id") {
        CHECK_THROWS_AS(parseCommandModelMaps(withIntents("{\"a\": 0, \"b\": 0}")),
                        CommandModelOnnxError);
    }
    SECTION("sparse map") {
        // One entry whose id skips slot 0: the table would hold a hole.
        CHECK_THROWS_AS(parseCommandModelMaps(withIntents("{\"a\": 1}")), CommandModelOnnxError);
    }
    SECTION("non-integer id") {
        CHECK_THROWS_AS(parseCommandModelMaps(withIntents("{\"a\": 0.5, \"b\": 0}")),
                        CommandModelOnnxError);
        CHECK_THROWS_AS(parseCommandModelMaps(withIntents("{\"a\": \"0\", \"b\": 1}")),
                        CommandModelOnnxError);
    }
    SECTION("missing or empty map objects") {
        CHECK_THROWS_AS(parseCommandModelMaps("{\"intents\": {\"a\": 0}}"), CommandModelOnnxError);
        CHECK_THROWS_AS(parseCommandModelMaps("{\"intents\": {}, \"tags\": {\"O\": 0}}"),
                        CommandModelOnnxError);
    }
    SECTION("unparseable text") {
        CHECK_THROWS_AS(parseCommandModelMaps(""), CommandModelOnnxError);
        CHECK_THROWS_AS(parseCommandModelMaps("not json"), CommandModelOnnxError);
    }
}
