#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/CuratedAliasLoader.hpp"

using namespace magda;

// ============================================================================
// Fixture JSON strings (in-memory, no BinaryData dependency)
//
// These represent third-party JSON-driven curated packs. MAGDA's own internal
// plugin aliases (eq, compressor, ...) ship from code via InternalPluginAliases
// and are exercised by the dedicated test below; the JSON path tested here is
// what would be used for a future Pro-Q / Diva / Serum etc. pack.
// ============================================================================

static const juce::String kIndexJson = R"({
  "version": 1,
  "plugins": [
    {
      "match": { "name_equals": "Test Equaliser", "format": "VST3" },
      "key": "test_eq",
      "file": "curated_test_eq_json"
    },
    {
      "match": { "name_equals": "Test Compressor", "format": "VST3" },
      "key": "test_compressor",
      "file": "curated_test_compressor_json"
    }
  ]
})";

static const juce::String kEqJson = R"({
  "version": 1,
  "pluginKey": "test_eq",
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
  "pluginKey": "test_compressor",
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
    if (filename == "curated_test_eq_json")
        return kEqJson;
    if (filename == "curated_test_compressor_json")
        return kCompressorJson;
    return {};
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("CuratedAliasLoader - third-party EQ pack loaded into Curated layer",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);

    // Keys should be {pluginKey}.{aliasKey}
    auto lowFreq = reg.lookupStored("test_eq.low_shelf_freq");
    REQUIRE(lowFreq.has_value());
    REQUIRE(lowFreq->paramIndex == 0);
    REQUIRE(lowFreq->pluginTypeKey == "test_eq");
    REQUIRE(lowFreq->paramNameAtSetTime == "Low-shelf freq");
    REQUIRE_FALSE(lowFreq->path.has_value());  // curated = no concrete path

    auto highFreq = reg.lookupStored("test_eq.high_shelf_freq");
    REQUIRE(highFreq.has_value());
    REQUIRE(highFreq->paramIndex == 9);
    REQUIRE(highFreq->paramNameAtSetTime == "High-shelf freq");

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - third-party Compressor pack loaded into Curated layer",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);

    auto threshold = reg.lookupStored("test_compressor.threshold");
    REQUIRE(threshold.has_value());
    REQUIRE(threshold->paramIndex == 0);
    REQUIRE(threshold->pluginTypeKey == "test_compressor");
    REQUIRE(threshold->paramNameAtSetTime == "Threshold");

    auto makeupGain = reg.lookupStored("test_compressor.makeup_gain");
    REQUIRE(makeupGain.has_value());
    REQUIRE(makeupGain->paramIndex == 4);
    REQUIRE(makeupGain->paramNameAtSetTime == "Output gain");

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - curated aliases are path-absent (no ControlTarget from lookup)",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::AutoGen);

    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);

    // lookup() requires a path; curated aliases have none, so result is nullopt
    auto result = reg.lookup("test_eq.low_shelf_freq");
    REQUIRE_FALSE(result.has_value());

    // lookupStored() returns the raw entry even without a path
    auto stored = reg.lookupStored("test_eq.low_shelf_freq");
    REQUIRE(stored.has_value());

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - replaceLayer clears old JSON entries on reload",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    // Load once
    CuratedAliasLoader::loadFromString(kIndexJson, testResolver);
    REQUIRE(reg.lookupStored("test_eq.low_shelf_freq").has_value());

    // Reload with empty index — JSON-driven entries must be gone, even though
    // code-driven internal aliases (eq.*, compressor.*) are still seeded.
    CuratedAliasLoader::loadFromString(R"({"version":1,"plugins":[]})", testResolver);
    REQUIRE_FALSE(reg.lookupStored("test_eq.low_shelf_freq").has_value());

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

    REQUIRE_FALSE(reg.lookupStored("test_eq.low_shelf_freq").has_value());

    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("CuratedAliasLoader - internal plugin aliases load from code without JSON",
          "[aliases][curated]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    auto noopResolver = [](const juce::String&) -> juce::String { return {}; };

    // Empty index, empty resolver — yet eq/compressor must be present because
    // they ship in InternalPluginAliases.cpp, not as JSON.
    CuratedAliasLoader::loadFromString(R"({"version":1,"plugins":[]})", noopResolver);

    auto eqLow = reg.lookupStored("eq.low_shelf_freq");
    REQUIRE(eqLow.has_value());
    REQUIRE(eqLow->pluginTypeKey == "eq");
    REQUIRE(eqLow->paramIndex == 0);

    auto compThreshold = reg.lookupStored("compressor.threshold");
    REQUIRE(compThreshold.has_value());
    REQUIRE(compThreshold->pluginTypeKey == "compressor");
    REQUIRE(compThreshold->paramIndex == 0);

    // Plugins added in the InternalPluginAliases expansion are also present.
    REQUIRE(reg.lookupStored("reverb.room_size").has_value());
    REQUIRE(reg.lookupStored("delay.feedback").has_value());
    REQUIRE(reg.lookupStored("utility.volume").has_value());

    reg.clearLayer(AliasLayer::Curated);
}
