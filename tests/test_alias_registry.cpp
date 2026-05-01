#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/AliasRegistry.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

static StoredAlias makeAlias(const juce::String& pluginType, int paramIdx,
                             const juce::String& paramName,
                             std::optional<ChainNodePath> path = std::nullopt) {
    StoredAlias a;
    a.pluginTypeKey = pluginType;
    a.paramIndex = paramIdx;
    a.paramNameAtSetTime = paramName;
    a.path = path;
    return a;
}

static ChainNodePath makePath(int trackId, int rackId, int chainId, int deviceId) {
    return ChainNodePath::chainDevice(trackId, rackId, chainId, deviceId);
}

// ============================================================================
// Layer precedence
// ============================================================================

TEST_CASE("AliasRegistry - layer precedence: UserProject beats UserGlobal", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();

    // Clean up
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);

    auto path1 = makePath(1, 10, 20, 30);
    auto path2 = makePath(2, 11, 21, 31);

    reg.set(AliasLayer::UserGlobal, "cutoff", makeAlias("serum", 5, "Cutoff Frequency", path2));
    reg.set(AliasLayer::UserProject, "cutoff", makeAlias("serum", 3, "Filter Cutoff", path1));

    auto result = reg.lookup("cutoff");
    REQUIRE(result.has_value());
    REQUIRE(result->paramIndex == 3);
    REQUIRE(result->devicePath == path1);

    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
}

TEST_CASE("AliasRegistry - UserGlobal wins over Curated", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();

    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);

    auto path1 = makePath(1, 10, 20, 30);
    auto path2 = makePath(2, 11, 21, 31);

    reg.set(AliasLayer::Curated, "resonance", makeAlias("serum", 6, "Resonance", path2));
    reg.set(AliasLayer::UserGlobal, "resonance", makeAlias("serum", 7, "Resonance", path1));

    auto result = reg.lookup("resonance");
    REQUIRE(result.has_value());
    REQUIRE(result->paramIndex == 7);

    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
}

TEST_CASE("AliasRegistry - missing name returns nullopt", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);

    REQUIRE_FALSE(reg.lookup("nonexistent_alias").has_value());
    REQUIRE_FALSE(reg.lookupStored("nonexistent_alias").has_value());
}

TEST_CASE("AliasRegistry - path-absent alias excluded from lookup but returned by lookupStored",
          "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserGlobal);

    // No path -- user-global alias without concrete location
    reg.set(AliasLayer::UserGlobal, "filter_cutoff",
            makeAlias("serum", 3, "Filter Cutoff"));  // path = nullopt

    auto staticResult = reg.lookup("filter_cutoff");
    REQUIRE_FALSE(staticResult.has_value());  // no path -> no ControlTarget

    auto storedResult = reg.lookupStored("filter_cutoff");
    REQUIRE(storedResult.has_value());
    REQUIRE(storedResult->paramIndex == 3);
    REQUIRE_FALSE(storedResult->path.has_value());

    reg.clearLayer(AliasLayer::UserGlobal);
}

// ============================================================================
// clearLayer / clear
// ============================================================================

TEST_CASE("AliasRegistry - clearLayer removes all entries", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    reg.set(AliasLayer::Curated, "a", makeAlias("p1", 1, "A"));
    reg.set(AliasLayer::Curated, "b", makeAlias("p1", 2, "B"));

    reg.clearLayer(AliasLayer::Curated);
    REQUIRE_FALSE(reg.lookupStored("a").has_value());
    REQUIRE_FALSE(reg.lookupStored("b").has_value());
}

TEST_CASE("AliasRegistry - clear removes single entry", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::AutoGen);

    reg.set(AliasLayer::AutoGen, "a", makeAlias("p1", 1, "A"));
    reg.set(AliasLayer::AutoGen, "b", makeAlias("p1", 2, "B"));
    reg.clear(AliasLayer::AutoGen, "a");

    REQUIRE_FALSE(reg.lookupStored("a").has_value());
    REQUIRE(reg.lookupStored("b").has_value());

    reg.clearLayer(AliasLayer::AutoGen);
}

// ============================================================================
// replaceLayer
// ============================================================================

TEST_CASE("AliasRegistry - replaceLayer atomically replaces contents", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::AutoGen);

    reg.set(AliasLayer::AutoGen, "old_key", makeAlias("p1", 0, "Old"));

    std::map<juce::String, StoredAlias> newEntries;
    newEntries["new_key"] = makeAlias("p2", 7, "New");
    reg.replaceLayer(AliasLayer::AutoGen, newEntries);

    REQUIRE_FALSE(reg.lookupStored("old_key").has_value());
    REQUIRE(reg.lookupStored("new_key").has_value());
    REQUIRE(reg.lookupStored("new_key")->paramIndex == 7);

    reg.clearLayer(AliasLayer::AutoGen);
}

// ============================================================================
// pluginTypeHint filtering
// ============================================================================

TEST_CASE("AliasRegistry - pluginTypeHint filters results", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::Curated);

    auto path1 = makePath(1, 10, 20, 30);
    reg.set(AliasLayer::Curated, "cutoff", makeAlias("serum", 3, "Cutoff", path1));

    // Correct hint -> hits
    REQUIRE(reg.lookup("cutoff", "serum").has_value());
    REQUIRE(reg.lookup("cutoff").has_value());  // no hint -> accepts any

    // Wrong hint -> no hit
    REQUIRE_FALSE(reg.lookup("cutoff", "surge_xt").has_value());

    reg.clearLayer(AliasLayer::Curated);
}

// ============================================================================
// JSON round-trip: StoredAlias serialization helpers
// ============================================================================

TEST_CASE("serializeStoredAlias/deserializeStoredAlias - round-trip with path",
          "[aliases][registry]") {
    StoredAlias original;
    original.pluginTypeKey = "serum";
    original.paramIndex = 7;
    original.paramNameAtSetTime = "Filter Cutoff";
    original.path = makePath(1, 10, 20, 30);

    auto json = serializeStoredAlias(original);
    StoredAlias restored;
    REQUIRE(deserializeStoredAlias(json, restored));

    REQUIRE(restored.pluginTypeKey == "serum");
    REQUIRE(restored.paramIndex == 7);
    REQUIRE(restored.paramNameAtSetTime == "Filter Cutoff");
    REQUIRE(restored.path.has_value());
    REQUIRE(*restored.path == *original.path);
}

TEST_CASE("serializeStoredAlias/deserializeStoredAlias - round-trip without path",
          "[aliases][registry]") {
    StoredAlias original;
    original.pluginTypeKey = "surge_xt";
    original.paramIndex = 2;
    original.paramNameAtSetTime = "Osc Pitch";
    // no path

    auto json = serializeStoredAlias(original);
    StoredAlias restored;
    REQUIRE(deserializeStoredAlias(json, restored));

    REQUIRE(restored.pluginTypeKey == "surge_xt");
    REQUIRE(restored.paramIndex == 2);
    REQUIRE_FALSE(restored.path.has_value());
}

// ============================================================================
// UserGlobal persistence round-trip (loadUserGlobal / saveUserGlobal)
// ============================================================================

TEST_CASE("AliasRegistry - UserGlobal round-trip via JSON var", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserGlobal);

    reg.set(AliasLayer::UserGlobal, "filter_cutoff", makeAlias("serum", 3, "Filter Cutoff"));
    reg.set(AliasLayer::UserGlobal, "reverb_size",
            makeAlias("valhalla", 5, "Reverb Size", makePath(2, 10, 20, 30)));

    // Save
    auto saved = reg.saveUserGlobal();

    // Clear and reload
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.loadUserGlobal(saved);

    auto fc = reg.lookupStored("filter_cutoff");
    REQUIRE(fc.has_value());
    REQUIRE(fc->paramIndex == 3);
    REQUIRE(fc->pluginTypeKey == "serum");
    REQUIRE_FALSE(fc->path.has_value());

    auto rs = reg.lookupStored("reverb_size");
    REQUIRE(rs.has_value());
    REQUIRE(rs->paramIndex == 5);
    REQUIRE(rs->path.has_value());

    reg.clearLayer(AliasLayer::UserGlobal);
}

// ============================================================================
// UserProject persistence round-trip (loadFromProjectJson / toProjectJson)
// ============================================================================

TEST_CASE("AliasRegistry - UserProject round-trip via JSON var", "[aliases][registry]") {
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);

    reg.set(AliasLayer::UserProject, "delay_time",
            makeAlias("fab_filter", 8, "Delay Time", makePath(3, 15, 25, 35)));

    auto saved = reg.toProjectJson();

    reg.clearLayer(AliasLayer::UserProject);
    reg.loadFromProjectJson(saved);

    auto dt = reg.lookupStored("delay_time");
    REQUIRE(dt.has_value());
    REQUIRE(dt->paramIndex == 8);
    REQUIRE(dt->pluginTypeKey == "fab_filter");
    REQUIRE(dt->path.has_value());

    reg.clearLayer(AliasLayer::UserProject);
}

// ============================================================================
// Listener notification
// ============================================================================

TEST_CASE("AliasRegistry - listener is notified on changes", "[aliases][registry]") {
    struct TestListener : AliasRegistryListener {
        int callCount = 0;
        AliasLayer lastLayer = AliasLayer::AutoGen;
        void aliasRegistryChanged(AliasLayer layer) override {
            ++callCount;
            lastLayer = layer;
        }
    };

    auto& reg = AliasRegistry::getInstance();
    TestListener listener;
    reg.addListener(&listener);

    reg.set(AliasLayer::UserProject, "test_param", makeAlias("p1", 0, "Test"));
    REQUIRE(listener.callCount == 1);
    REQUIRE(listener.lastLayer == AliasLayer::UserProject);

    reg.clear(AliasLayer::UserProject, "test_param");
    REQUIRE(listener.callCount == 2);

    reg.clearLayer(AliasLayer::UserProject);
    REQUIRE(listener.callCount == 3);

    reg.removeListener(&listener);
}
