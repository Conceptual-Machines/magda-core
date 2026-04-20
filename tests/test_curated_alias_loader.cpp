#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/CuratedAliasLoader.hpp"

using namespace magda;

// ============================================================================
// Fixture JSON strings (in-memory, no BinaryData dependency)
// ============================================================================

static const juce::String kIndexJson = R"({
  "version": 1,
  "plugins": [
    {
      "match": { "name_equals": "Equaliser", "format": "Internal" },
      "key": "eq",
      "file": "curated_eq_json"
    },
    {
      "match": { "name_equals": "Compressor", "format": "Internal" },
      "key": "compressor",
      "file": "curated_compressor_json"
    }
  ]
})";

static const juce::String kEqJson = R"({
  "version": 1,
  "pluginKey": "eq",
  "aliases": {
    "low_shelf_freq": { "paramIndex": 0 },
    "low_shelf_gain": { "paramIndex": 1 },
    "high_shelf_freq": { "paramIndex": 9 }
  },
  "aliasesByName": {
    "low_shelf_freq": ["Low-shelf freq", "Low-pass freq"],
    "low_shelf_gain": ["Low-shelf gain"],
    "high_shelf_freq": ["High-shelf freq", "High-pass freq"]
  }
})";

static const juce::String kCompressorJson = R"({
  "version": 1,
  "pluginKey": "compressor",
  "aliases": {
    "threshold": { "paramIndex": 0 },
    "ratio":     { "paramIndex": 1 },
    "attack":    { "paramIndex": 2 },
    "release":   { "paramIndex": 3 },
    "makeup_gain": { "paramIndex": 4 }
  },
  "aliasesByName": {
    "threshold":   ["Threshold"],
    "ratio":       ["Ratio"],
    "attack":      ["Attack"],
    "release":     ["Release"],
    "makeup_gain": ["Output gain", "Makeup Gain"]
  }
})";

// ============================================================================
// File resolver for tests
// ============================================================================

static juce::String testResolver(const juce::String& filename) {
    if (filename == "curated_eq_json")
        return kEqJson;
    if (filename == "curated_compressor_json")
        return kCompressorJson;
    return {};
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("CuratedAliasLoader - EQ aliases loaded into Curated layer", "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);

    // Keys should be {pluginKey}.{aliasKey}
    auto lowFreq = reg.lookupStored("eq.low_shelf_freq");
    REQUIRE(lowFreq.has_value());
    REQUIRE(lowFreq->paramIndex == 0);
    REQUIRE(lowFreq->pluginTypeKey == "eq");
    REQUIRE(lowFreq->paramNameAtSetTime == "Low-shelf freq");
    REQUIRE_FALSE(lowFreq->path.has_value());  // curated = no concrete path

    auto highFreq = reg.lookupStored("eq.high_shelf_freq");
    REQUIRE(highFreq.has_value());
    REQUIRE(highFreq->paramIndex == 9);
    REQUIRE(highFreq->paramNameAtSetTime == "High-shelf freq");

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - Compressor aliases loaded into Curated layer",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);

    auto threshold = reg.lookupStored("compressor.threshold");
    REQUIRE(threshold.has_value());
    REQUIRE(threshold->paramIndex == 0);
    REQUIRE(threshold->pluginTypeKey == "compressor");
    REQUIRE(threshold->paramNameAtSetTime == "Threshold");

    auto makeupGain = reg.lookupStored("compressor.makeup_gain");
    REQUIRE(makeupGain.has_value());
    REQUIRE(makeupGain->paramIndex == 4);
    REQUIRE(makeupGain->paramNameAtSetTime == "Output gain");

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - curated aliases are path-absent (no StaticTarget from lookup)",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::AutoGen);

    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);

    // lookup() requires a path; curated aliases have none, so result is nullopt
    auto result = reg.lookup("eq.low_shelf_freq");
    REQUIRE_FALSE(result.has_value());

    // lookupStored() returns the raw entry even without a path
    auto stored = reg.lookupStored("eq.low_shelf_freq");
    REQUIRE(stored.has_value());

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - replaceLayer clears old entries on reload", "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    // Load once
    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);
    REQUIRE(reg.lookupStored("eq.low_shelf_freq").has_value());

    // Reload with empty index
    CuratedAliasLoader::loadFromString(R"({"version":1,"plugins":[]})", testResolver);
    REQUIRE_FALSE(reg.lookupStored("eq.low_shelf_freq").has_value());

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - malformed index JSON is ignored gracefully", "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    // Should not throw or crash
    CuratedAliasLoader::loadFromString("not valid json{{{", testResolver);

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - missing plugin file is silently skipped", "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    // Resolver that never finds files
    auto emptyResolver = [](const juce::String&) -> juce::String { return {}; };

    CuratedAliasLoader::loadFromString(kIndexJson, emptyResolver);

    REQUIRE_FALSE(reg.lookupStored("eq.low_shelf_freq").has_value());

    reg.clearLayer(AliasLayer::Curated);
}
